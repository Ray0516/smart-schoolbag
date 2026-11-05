import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:math' as math;

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:flutter_localizations/flutter_localizations.dart';
import 'package:flutter/services.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:flutter_local_notifications/flutter_local_notifications.dart';
import 'package:flutter_timezone/flutter_timezone.dart';
import 'package:intl/intl.dart';
import 'package:intl/date_symbol_data_local.dart' as intl_data;
import 'package:permission_handler/permission_handler.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:timezone/data/latest.dart' as tzdata;
import 'package:timezone/timezone.dart' as tz;

const serviceUuid = '8c3a0001-9c4a-4f2d-a2e1-6a4b53424701';
const controlUuid = '8c3a0002-9c4a-4f2d-a2e1-6a4b53424701';
const eventUuid = '8c3a0003-9c4a-4f2d-a2e1-6a4b53424701';
const appTitle = '智慧電子書包';

String normalizeItemName(String input) {
  final cleaned = input.replaceAll(RegExp(r'[\t\r\n]+'), ' ').trim();
  final result = StringBuffer();
  var byteCount = 0;
  for (final rune in cleaned.runes) {
    final character = String.fromCharCode(rune);
    final size = utf8.encode(character).length;
    if (byteCount + size > 60) break;
    result.write(character);
    byteCount += size;
  }
  return result.toString();
}

class BagItem {
  BagItem(
    String name, {
    String tag = '',
    bool checked = false,
    List<int>? checkedWeekdays,
    this.category = 'item',
    this.carryRule = 'auto',
    List<int>? carryWeekdays,
    List<String>? carryDates,
  }) : name = normalizeItemName(name),
       tag = tag.trim().toUpperCase(),
       checkedWeekdays =
           checkedWeekdays ??
           (checked ? <int>[DateTime.now().weekday] : <int>[]),
       carryWeekdays = carryWeekdays ?? <int>[],
       carryDates = carryDates ?? <String>[];
  String name;
  String tag;
  List<int> checkedWeekdays;
  bool get checked => checkedOn(DateTime.now().weekday);
  set checked(bool value) => setCheckedOn(DateTime.now().weekday, value);
  bool checkedOn(int weekday) => checkedWeekdays.contains(weekday);
  void setCheckedOn(int weekday, bool value) {
    if (weekday < 1 || weekday > 7) return;
    if (value && !checkedWeekdays.contains(weekday)) {
      checkedWeekdays.add(weekday);
      checkedWeekdays.sort();
    } else if (!value) {
      checkedWeekdays.remove(weekday);
    }
  }

  String category;
  bool get isBook => category == 'book';
  String carryRule;
  List<int> carryWeekdays;
  List<String> carryDates;
  bool scheduledOn(DateTime day) {
    switch (carryRule) {
      case 'daily':
        return true;
      case 'weekdays':
        return carryWeekdays.contains(day.weekday);
      case 'dates':
        return carryDates.contains(DateFormat('yyyy-MM-dd').format(day));
      default:
        return false;
    }
  }

  Map<String, dynamic> toJson() => {
    'name': name,
    'tag': tag,
    'checked': checked,
    'checkedWeekdays': checkedWeekdays,
    'category': category,
    'carryRule': carryRule,
    'carryWeekdays': carryWeekdays,
    'carryDates': carryDates,
  };
  factory BagItem.fromJson(Map<String, dynamic> json) => BagItem(
    json['name'] as String? ?? '未命名物品',
    tag: json['tag'] as String? ?? '',
    category:
        json['category'] == 'book' ||
            (!json.containsKey('category') &&
                (json['name'] as String? ?? '').contains('課本'))
        ? 'book'
        : 'item',
    checked: json['checked'] as bool? ?? false,
    checkedWeekdays: json.containsKey('checkedWeekdays')
        ? (json['checkedWeekdays'] as List<dynamic>? ?? const [])
              .whereType<int>()
              .where((day) => day >= 1 && day <= 7)
              .toSet()
              .toList()
        : null,
    carryRule:
        const {'auto', 'daily', 'weekdays', 'dates'}.contains(json['carryRule'])
        ? json['carryRule'] as String
        : 'auto',
    carryWeekdays: (json['carryWeekdays'] as List<dynamic>? ?? const [])
        .whereType<int>()
        .where((day) => day >= 1 && day <= 7)
        .toList(),
    carryDates: (json['carryDates'] as List<dynamic>? ?? const [])
        .whereType<String>()
        .where((date) {
          final match = RegExp(r'^(\d{4})-(\d{2})-(\d{2})$').firstMatch(date);
          if (match == null) return false;
          final year = int.parse(match[1]!);
          final month = int.parse(match[2]!);
          final day = int.parse(match[3]!);
          final parsed = DateTime(year, month, day);
          return parsed.year == year &&
              parsed.month == month &&
              parsed.day == day;
        })
        .toList(),
  );
}

String carryScheduleLabel(BagItem item) {
  switch (item.carryRule) {
    case 'daily':
      return '每天必帶';
    case 'weekdays':
      final names = ['一', '二', '三', '四', '五', '六', '日'];
      return '每週${item.carryWeekdays.map((d) => names[d - 1]).join('、')}必帶';
    case 'dates':
      return '指定日期 ${item.carryDates.length} 天';
    default:
      return '依課表／行事';
  }
}

class Lesson {
  Lesson({
    required this.weekday,
    required this.period,
    required this.subject,
    required this.itemNames,
  });
  int weekday;
  int period;
  String subject;
  List<String> itemNames;
  Map<String, dynamic> toJson() => {
    'weekday': weekday,
    'period': period,
    'subject': subject,
    'items': itemNames,
  };
  factory Lesson.fromJson(Map<String, dynamic> j) => Lesson(
    weekday: j['weekday'] as int? ?? 1,
    period: j['period'] as int? ?? 1,
    subject: j['subject'] as String? ?? '',
    itemNames: (j['items'] as List<dynamic>? ?? const [])
        .whereType<String>()
        .toList(),
  );
}

class CalendarEntry {
  CalendarEntry({
    required this.title,
    required this.date,
    String? uid,
    this.description = '',
    this.remind = true,
    this.rrule = '',
  }) : uid = uid == null || uid.isEmpty ? _newCalendarUid() : uid;
  String uid;
  String title;
  DateTime date;
  String description;
  bool remind;
  String rrule;
  Map<String, dynamic> toJson() => {
    'uid': uid,
    'title': title,
    'date': date.toIso8601String(),
    'description': description,
    'remind': remind,
    'rrule': rrule,
  };
  factory CalendarEntry.fromJson(Map<String, dynamic> j) => CalendarEntry(
    title: j['title'] as String? ?? '',
    date: DateTime.tryParse(j['date'] as String? ?? '') ?? DateTime.now(),
    uid: j['uid'] as String?,
    description: j['description'] as String? ?? '',
    remind: j['remind'] as bool? ?? true,
    rrule: j['rrule'] as String? ?? '',
  );
}

int _calendarUidCounter = 0;
String _newCalendarUid() =>
    '${DateTime.now().microsecondsSinceEpoch}-${_calendarUidCounter++}@smart-schoolbag.invalid';

bool occursOnDate(CalendarEntry event, DateTime day) {
  final start = DateTime(event.date.year, event.date.month, event.date.day);
  final target = DateTime(day.year, day.month, day.day);
  if (target.isBefore(start)) return false;
  final frequency = RegExp(
    r'FREQ=(DAILY|WEEKLY|MONTHLY)',
    caseSensitive: false,
  ).firstMatch(event.rrule)?.group(1)?.toUpperCase();
  if (frequency == null) return DateUtils.isSameDay(start, target);
  final interval =
      int.tryParse(
        RegExp(
              r'INTERVAL=(\d+)',
              caseSensitive: false,
            ).firstMatch(event.rrule)?.group(1) ??
            '1',
      ) ??
      1;
  if (interval < 1) return false;
  final startUtc = DateTime.utc(start.year, start.month, start.day);
  final targetUtc = DateTime.utc(target.year, target.month, target.day);
  final deltaDays = targetUtc.difference(startUtc).inDays;
  int occurrenceIndex;
  switch (frequency) {
    case 'DAILY':
      if (deltaDays % interval != 0) return false;
      occurrenceIndex = deltaDays ~/ interval;
      break;
    case 'WEEKLY':
      if (target.weekday != start.weekday) return false;
      final deltaWeeks = deltaDays ~/ 7;
      if (deltaWeeks % interval != 0) return false;
      occurrenceIndex = deltaWeeks ~/ interval;
      break;
    case 'MONTHLY':
      final deltaMonths =
          (target.year - start.year) * 12 + target.month - start.month;
      if (target.day != start.day || deltaMonths % interval != 0) {
        return false;
      }
      occurrenceIndex = 0;
      for (
        var monthOffset = 0;
        monthOffset <= deltaMonths;
        monthOffset += interval
      ) {
        final monthNumber = start.month - 1 + monthOffset;
        final year = start.year + monthNumber ~/ 12;
        final month = monthNumber % 12 + 1;
        final lastDay = DateTime(year, month + 1, 0).day;
        if (start.day <= lastDay) {
          if (monthOffset == deltaMonths) break;
          occurrenceIndex++;
        }
      }
      break;
    default:
      return false;
  }
  final count = int.tryParse(
    RegExp(
          r'COUNT=(\d+)',
          caseSensitive: false,
        ).firstMatch(event.rrule)?.group(1) ??
        '',
  );
  if (count != null && occurrenceIndex >= count) return false;
  final until = RegExp(
    r'UNTIL=(\d{8}(?:T\d{6}Z?)?)',
    caseSensitive: false,
  ).firstMatch(event.rrule)?.group(1);
  if (until != null) {
    final match = RegExp(
      r'^(\d{4})(\d{2})(\d{2})(?:T(\d{2})(\d{2})(\d{2})(Z?))?',
    ).firstMatch(until);
    if (match != null) {
      final values = [
        int.parse(match[1]!),
        int.parse(match[2]!),
        int.parse(match[3]!),
        int.parse(match[4] ?? '23'),
        int.parse(match[5] ?? '59'),
        int.parse(match[6] ?? '59'),
      ];
      final cutoff = match[7] == 'Z'
          ? DateTime.utc(
              values[0],
              values[1],
              values[2],
              values[3],
              values[4],
              values[5],
            ).toLocal()
          : DateTime(
              values[0],
              values[1],
              values[2],
              values[3],
              values[4],
              values[5],
            );
      final occurrence = DateTime(
        target.year,
        target.month,
        target.day,
        event.date.hour,
        event.date.minute,
        event.date.second,
      );
      if (occurrence.isAfter(cutoff)) return false;
    }
  }
  return true;
}

class BagStore extends ChangeNotifier {
  final items = <BagItem>[];
  final dayItemOverrides = <int, List<String>>{};
  final lessons = <Lesson>[];
  final events = <CalendarEntry>[];
  bool locationSharing = false;
  bool safeZoneAlertsEnabled = false;
  double? safeZoneLatitude;
  double? safeZoneLongitude;
  double safeZoneRadiusMeters = 200;
  bool? outsideSafeZone;
  int _outsideFixCount = 0;
  int _insideFixCount = 0;
  double? latestSafeZoneDistanceMeters;
  bool resetToday = false;
  DateTime? lastSync;
  String deviceStatus = '尚未連線';
  String gpsStatus = '尚未同步';
  String lastRfid = '';
  final stickyNotes = <String>[];
  String get stickyNote => stickyNotes.isEmpty ? '' : stickyNotes.first;
  set stickyNote(String value) {
    final note = value.trim();
    if (stickyNotes.isEmpty) {
      if (note.isNotEmpty) stickyNotes.add(note);
    } else if (note.isEmpty) {
      stickyNotes.removeAt(0);
    } else {
      stickyNotes[0] = note;
    }
  }

  Future<void> load() async {
    final p = await SharedPreferences.getInstance();
    try {
      items.addAll(
        (jsonDecode(p.getString('items') ?? '[]') as List).map(
          (e) => BagItem.fromJson(Map<String, dynamic>.from(e)),
        ),
      );
    } catch (_) {}
    try {
      lessons.addAll(
        (jsonDecode(p.getString('lessons') ?? '[]') as List).map(
          (e) => Lesson.fromJson(Map<String, dynamic>.from(e)),
        ),
      );
    } catch (_) {}
    try {
      events.addAll(
        (jsonDecode(p.getString('events') ?? '[]') as List).map(
          (e) => CalendarEntry.fromJson(Map<String, dynamic>.from(e)),
        ),
      );
    } catch (_) {}
    try {
      final stored = jsonDecode(p.getString('dayItemOverrides') ?? '{}') as Map;
      for (final entry in stored.entries) {
        final day = int.tryParse(entry.key.toString());
        if (day != null && day >= 1 && day <= 7 && entry.value is List) {
          dayItemOverrides[day] = (entry.value as List)
              .whereType<String>()
              .toList();
        }
      }
    } catch (_) {}
    final eventIds = <String>{};
    for (final event in events) {
      if (!eventIds.add(event.uid)) event.uid = _newCalendarUid();
    }
    locationSharing = p.getBool('locationSharing') ?? false;
    safeZoneAlertsEnabled =
        p.getBool('safeZoneAlertsEnabled') ?? locationSharing;
    safeZoneLatitude = p.getDouble('safeZoneLatitude');
    safeZoneLongitude = p.getDouble('safeZoneLongitude');
    safeZoneRadiusMeters = (p.getDouble('safeZoneRadiusMeters') ?? 200)
        .clamp(50, 5000)
        .toDouble();
    lastSync = DateTime.tryParse(p.getString('lastSync') ?? '');
    try {
      stickyNotes.addAll(
        (jsonDecode(p.getString('stickyNotes') ?? '[]') as List)
            .whereType<String>()
            .map((note) => note.trim())
            .where((note) => note.isNotEmpty)
            .take(5),
      );
    } catch (_) {}
    if (stickyNotes.isEmpty) {
      final legacyNote = (p.getString('stickyNote') ?? '').trim();
      if (legacyNote.isNotEmpty) stickyNotes.add(legacyNote);
    }
    final seeded = p.getBool('seeded') ?? false;
    final todayKey = DateFormat('yyyy-MM-dd').format(DateTime.now());
    resetToday = p.getString('checklistDate') != todayKey;
    if (resetToday) {
      for (final item in items) {
        item.checked = false;
      }
    }
    if (items.isEmpty && !seeded) {
      items.addAll(
        [
          '國文課本',
          '英文課本',
          '自然課本',
          '作業本',
          '水壺',
        ].map((s) => BagItem(s, category: s.contains('課本') ? 'book' : 'item')),
      );
    }
    if (lessons.isEmpty && !seeded) {
      lessons.addAll([
        Lesson(
          weekday: 1,
          period: 1,
          subject: '國文',
          itemNames: ['國文課本', '作業本'],
        ),
        Lesson(weekday: 1, period: 2, subject: '英文', itemNames: ['英文課本']),
        Lesson(weekday: 1, period: 3, subject: '自然', itemNames: ['自然課本']),
        Lesson(weekday: 2, period: 1, subject: '數學', itemNames: ['作業本']),
      ]);
    }
    notifyListeners();
  }

  Future<void> save() async {
    final p = await SharedPreferences.getInstance();
    await p.setString(
      'dayItemOverrides',
      jsonEncode(dayItemOverrides.map((day, names) => MapEntry('$day', names))),
    );
    await p.setString(
      'items',
      jsonEncode(items.map((e) => e.toJson()).toList()),
    );
    await p.setString(
      'lessons',
      jsonEncode(lessons.map((e) => e.toJson()).toList()),
    );
    await p.setString(
      'events',
      jsonEncode(events.map((e) => e.toJson()).toList()),
    );
    await p.setString('stickyNote', stickyNote);
    await p.setString('stickyNotes', jsonEncode(stickyNotes));
    await p.setBool('locationSharing', locationSharing);
    await p.setBool('safeZoneAlertsEnabled', safeZoneAlertsEnabled);
    if (safeZoneLatitude != null && safeZoneLongitude != null) {
      await p.setDouble('safeZoneLatitude', safeZoneLatitude!);
      await p.setDouble('safeZoneLongitude', safeZoneLongitude!);
    } else {
      await p.remove('safeZoneLatitude');
      await p.remove('safeZoneLongitude');
    }
    await p.setDouble('safeZoneRadiusMeters', safeZoneRadiusMeters);
    if (lastSync != null) {
      await p.setString('lastSync', lastSync!.toIso8601String());
    } else {
      await p.remove('lastSync');
    }
    await p.setBool('seeded', true);
    await p.setString(
      'checklistDate',
      DateFormat('yyyy-MM-dd').format(DateTime.now()),
    );
    notifyListeners();
  }

  Future<void> setSafeZoneCenter(double latitude, double longitude) async {
    safeZoneLatitude = latitude;
    safeZoneLongitude = longitude;
    outsideSafeZone = null;
    _outsideFixCount = 0;
    _insideFixCount = 0;
    latestSafeZoneDistanceMeters = null;
    await save();
  }

  Future<void> setSafeZoneAlertsEnabled(bool enabled) async {
    safeZoneAlertsEnabled = enabled;
    locationSharing = enabled; // Keep compatibility with earlier local backups.
    outsideSafeZone = null;
    _outsideFixCount = 0;
    _insideFixCount = 0;
    latestSafeZoneDistanceMeters = null;
    await save();
  }

  Future<void> setSafeZoneRadius(double radiusMeters) async {
    safeZoneRadiusMeters = radiusMeters.clamp(50, 5000).toDouble();
    outsideSafeZone = null;
    _outsideFixCount = 0;
    _insideFixCount = 0;
    await save();
  }

  void previewSafeZoneRadius(double radiusMeters) {
    safeZoneRadiusMeters = radiusMeters.clamp(50, 5000).toDouble();
    outsideSafeZone = null;
    _outsideFixCount = 0;
    _insideFixCount = 0;
    notifyListeners();
  }

  Future<void> updateDeviceLocation(double latitude, double longitude) async {
    final centerLat = safeZoneLatitude;
    final centerLon = safeZoneLongitude;
    if (!safeZoneAlertsEnabled || centerLat == null || centerLon == null) {
      return;
    }
    final distance = _distanceMeters(latitude, longitude, centerLat, centerLon);
    latestSafeZoneDistanceMeters = distance;
    if (distance > safeZoneRadiusMeters) {
      _outsideFixCount++;
      _insideFixCount = 0;
      if (_outsideFixCount >= 2 && outsideSafeZone != true) {
        outsideSafeZone = true;
        await ReminderService.showSafeZoneAlert(
          outside: true,
          distanceMeters: distance,
          radiusMeters: safeZoneRadiusMeters,
        );
      }
    } else {
      _insideFixCount++;
      _outsideFixCount = 0;
      if (_insideFixCount >= 2 && outsideSafeZone == true) {
        outsideSafeZone = false;
        await ReminderService.showSafeZoneAlert(
          outside: false,
          distanceMeters: distance,
          radiusMeters: safeZoneRadiusMeters,
        );
      } else if (_insideFixCount >= 2 && outsideSafeZone == null) {
        outsideSafeZone = false;
      }
    }
    notifyListeners();
  }

  static double _distanceMeters(
    double lat1,
    double lon1,
    double lat2,
    double lon2,
  ) {
    const earthRadiusMeters = 6371000.0;
    final dLat = (lat2 - lat1) * math.pi / 180;
    final dLon = (lon2 - lon1) * math.pi / 180;
    final rawA =
        math.pow(math.sin(dLat / 2), 2) +
        math.cos(lat1 * math.pi / 180) *
            math.cos(lat2 * math.pi / 180) *
            math.pow(math.sin(dLon / 2), 2);
    final a = rawA.clamp(0.0, 1.0).toDouble();
    return earthRadiusMeters * 2 * math.atan2(math.sqrt(a), math.sqrt(1 - a));
  }

  List<Lesson> lessonsFor(DateTime d) =>
      lessons.where((l) => l.weekday == d.weekday).toList()
        ..sort((a, b) => a.period.compareTo(b.period));
  List<BagItem> itemsFor(DateTime d) {
    final explicit = dayItemOverrides[d.weekday];
    if (explicit != null) {
      return items.where((item) => explicit.contains(item.name)).toList();
    }
    final names = <String>{};
    for (final l in lessonsFor(d)) {
      names.addAll(l.itemNames);
    }
    for (final e in events.where((e) => occursOnDate(e, d))) {
      if (e.description.trim().isNotEmpty) {
        names.addAll(
          e.description
              .split(RegExp(r'[,，、]'))
              .map((x) => x.trim())
              .where((x) => x.isNotEmpty),
        );
      }
    }
    final explicitlyScheduled = items.where((item) => item.scheduledOn(d));
    names.addAll(explicitlyScheduled.map((item) => item.name));
    final hasExplicitRules = items.any((item) => item.carryRule != 'auto');
    if (names.isEmpty) return hasExplicitRules ? [] : items;
    return items.where((item) => names.contains(item.name)).toList();
  }
}

class ReminderService {
  static final plugin = FlutterLocalNotificationsPlugin();
  static Future<void> showSafeZoneAlert({
    required bool outside,
    required double distanceMeters,
    required double radiusMeters,
  }) async {
    final distance = distanceMeters.round();
    final radius = radiusMeters.round();
    try {
      await plugin.show(
        id: outside ? 8801 : 8802,
        title: outside ? '書包已離開安全範圍' : '書包已返回安全範圍',
        body: outside
            ? '裝置距安全範圍中心約 $distance 公尺，設定半徑 $radius 公尺。'
            : '裝置距安全範圍中心約 $distance 公尺。',
        notificationDetails: const NotificationDetails(
          android: AndroidNotificationDetails(
            'schoolbag_safety',
            '安全範圍通知',
            channelDescription: '裝置 GPS 連線期間的安全範圍進出通知',
            importance: Importance.high,
            priority: Priority.high,
          ),
          iOS: DarwinNotificationDetails(),
        ),
      );
    } catch (_) {}
  }

  static Future<void> init() async {
    tzdata.initializeTimeZones();
    try {
      final localTimezone = await FlutterTimezone.getLocalTimezone();
      tz.setLocalLocation(tz.getLocation(localTimezone.identifier));
    } catch (_) {
      tz.setLocalLocation(tz.getLocation('Asia/Taipei'));
    }
    await plugin.initialize(
      settings: const InitializationSettings(
        android: AndroidInitializationSettings('@mipmap/ic_launcher'),
        iOS: DarwinInitializationSettings(),
      ),
    );
    final android = plugin
        .resolvePlatformSpecificImplementation<
          AndroidFlutterLocalNotificationsPlugin
        >();
    try {
      await android?.requestNotificationsPermission();
    } catch (_) {}
    final ios = plugin
        .resolvePlatformSpecificImplementation<
          IOSFlutterLocalNotificationsPlugin
        >();
    try {
      await ios?.requestPermissions(alert: true, badge: true, sound: true);
    } catch (_) {}
  }

  static Future<void> migrateNotificationIds() async {
    final prefs = await SharedPreferences.getInstance();
    if ((prefs.getInt('reminder_id_schema') ?? 0) < 1) {
      // Earlier app drafts derived IDs from title/date, so repeated events
      // could overwrite each other. Clear those stale schedules once.
      await plugin.cancelAll();
      await prefs.setInt('reminder_id_schema', 1);
    }
  }

  static Future<int> schedule(
    CalendarEntry e,
    int id, {
    int maxOccurrences = 30,
  }) async {
    await cancel(e, id);
    if (!e.remind) return 0;
    final freq =
        RegExp(
          r'FREQ=(DAILY|WEEKLY|MONTHLY)',
          caseSensitive: false,
        ).firstMatch(e.rrule)?.group(1)?.toUpperCase() ??
        '';
    final maxCount = freq.isEmpty ? 1 : maxOccurrences.clamp(0, 30).toInt();
    if (maxCount == 0) return 0;
    final ruleCount = int.tryParse(
      RegExp(
            r'COUNT=(\d+)',
            caseSensitive: false,
          ).firstMatch(e.rrule)?.group(1) ??
          '',
    );
    final untilText = RegExp(
      r'UNTIL=(\d{8}(?:T\d{6}Z)?)',
      caseSensitive: false,
    ).firstMatch(e.rrule)?.group(1);
    DateTime? until;
    if (untilText != null) {
      final m = RegExp(
        r'^(\d{4})(\d{2})(\d{2})(?:T(\d{2})(\d{2})(\d{2})Z?)?',
      ).firstMatch(untilText);
      if (m != null) {
        final values = [
          int.parse(m[1]!),
          int.parse(m[2]!),
          int.parse(m[3]!),
          int.parse(m[4] ?? '23'),
          int.parse(m[5] ?? '59'),
          int.parse(m[6] ?? '59'),
        ];
        until = untilText.endsWith('Z')
            ? DateTime.utc(
                values[0],
                values[1],
                values[2],
                values[3],
                values[4],
                values[5],
              ).toLocal()
            : DateTime(
                values[0],
                values[1],
                values[2],
                values[3],
                values[4],
                values[5],
              );
      }
    }
    if (freq.isNotEmpty &&
        freq != 'DAILY' &&
        freq != 'WEEKLY' &&
        freq != 'MONTHLY') {
      return 0;
    }
    final now = DateTime.now();
    final firstDay = DateTime(e.date.year, e.date.month, e.date.day);
    final normalHorizon = DateTime(now.year, now.month, now.day + 367);
    final endDay = firstDay.isAfter(normalHorizon)
        ? firstDay.add(const Duration(days: 367))
        : normalHorizon;
    var occurrenceIndex = 0;
    var futureScheduled = 0;
    final scheduledIds = <int>[];
    for (
      var day = firstDay;
      !day.isAfter(endDay);
      day = day.add(const Duration(days: 1))
    ) {
      if (!occursOnDate(e, day)) continue;
      final occurrence = DateTime(
        day.year,
        day.month,
        day.day,
        e.date.hour,
        e.date.minute,
        e.date.second,
      );
      if (until != null && occurrence.isAfter(until)) break;
      if (ruleCount != null && occurrenceIndex >= ruleCount) break;
      final local = DateTime(
        occurrence.year,
        occurrence.month,
        occurrence.day,
        occurrence.hour,
        occurrence.minute,
      ).subtract(const Duration(minutes: 30));
      if (!local.isBefore(now)) {
        final notificationId = (id + occurrenceIndex) & 0x7fffffff;
        await plugin.zonedSchedule(
          id: notificationId,
          title: '書包提醒：${e.title}',
          body: e.description.isEmpty ? '記得確認要帶的物品。' : e.description,
          scheduledDate: tz.TZDateTime.from(local, tz.local),
          notificationDetails: const NotificationDetails(
            android: AndroidNotificationDetails(
              'schoolbag_reminders',
              '書包提醒',
              channelDescription: '課表與行事曆提醒',
              importance: Importance.high,
              priority: Priority.high,
            ),
            iOS: DarwinNotificationDetails(),
          ),
          androidScheduleMode: AndroidScheduleMode.inexactAllowWhileIdle,
        );
        scheduledIds.add(notificationId);
        futureScheduled++;
        if (freq.isEmpty || futureScheduled >= maxCount) break;
      }
      occurrenceIndex++;
    }
    final prefs = await SharedPreferences.getInstance();
    await prefs.setStringList(
      _idsKey(e),
      scheduledIds.map((x) => '$x').toList(),
    );
    return futureScheduled;
  }

  static Future<void> scheduleAll(List<CalendarEntry> events) async {
    const platformPendingLimit = 60;
    var budget = platformPendingLimit;
    var remaining = events.where((event) => event.remind).length;
    for (final event in events) {
      if (!event.remind) {
        await cancel(event, _stableId(event));
        continue;
      }
      if (budget == 0) {
        await cancel(event, _stableId(event));
        remaining--;
        continue;
      }
      final share = (budget / remaining).ceil().clamp(1, 30).toInt();
      final placed = await schedule(
        event,
        _stableId(event),
        maxOccurrences: share,
      );
      budget -= placed;
      remaining--;
    }
  }

  static Future<void> cancel(CalendarEntry e, int id) async {
    final prefs = await SharedPreferences.getInstance();
    final ids = prefs.getStringList(_idsKey(e)) ?? const <String>[];
    for (final value in ids) {
      final notificationId = int.tryParse(value);
      if (notificationId != null) await plugin.cancel(id: notificationId);
    }
    await prefs.remove(_idsKey(e));
  }

  static Future<void> cancelAllEvents(Iterable<CalendarEntry> events) async {
    for (final event in events) {
      await cancel(event, _stableId(event));
    }
    await plugin.cancelAll();
  }

  static String _idsKey(CalendarEntry e) => 'reminder_ids_${e.uid}';
}

class BagBle extends ChangeNotifier {
  BluetoothDevice? device;
  BluetoothCharacteristic? control;
  StreamSubscription<List<ScanResult>>? scanSub;
  StreamSubscription<BluetoothConnectionState>? connectionSub;
  StreamSubscription<List<int>>? eventSub;
  bool scanning = false;
  bool wifiConnected = false;
  bool get connected => wifiConnected || control != null;
  static const String wifiBaseUrl = 'http://192.168.4.1';
  final HttpClient _httpClient = HttpClient()
    ..connectionTimeout = const Duration(seconds: 3)
    ..findProxy = (_) => 'DIRECT';
  Timer? _wifiStatusTimer;
  bool _wifiStatusPollInFlight = false;
  String status = '尚未連線';
  String gps = '尚未同步';
  String rfid = '';
  String firmwareVersion = '未知';
  String lastError = '';
  String notesSyncStatus = '尚未同步';
  List<String> deviceStickyNotes = const <String>[];
  List<String?>? _notesReadbackBuffer;
  int? _notesReadbackExpected;
  bool _legacyNotesReadback = false;
  Completer<void>? _notesReadbackDone;
  DateTime? gpsUpdatedAt;
  double? gpsLatitude;
  double? gpsLongitude;
  bool _userDisconnected = false;
  int _reconnectAttempts = 0;
  Timer? _reconnectTimer;
  Completer<void>? _syncDone;
  String? _syncError;
  Completer<void>? _payloadDone;
  String? _payloadExpected;
  String? _payloadError;
  Future<void> _commandTail = Future<void>.value();
  final found = <ScanResult>[];
  final lines = StreamController<String>.broadcast();
  final List<int> _eventBytes = <int>[];
  bool _pairingInProgress = false;
  bool _autoSyncRunning = false;
  bool _wifiSyncBusy = false;
  String _lastAutoSyncData = '';

  Future<void> autoSync(BagStore store) async {
    if (_autoSyncRunning || _pairingInProgress) return;
    final data = jsonEncode([
      DateFormat('yyyy-MM-dd').format(DateTime.now()),
      store.items.map((item) => item.toJson()).toList(),
      store.lessons.map((lesson) => lesson.toJson()).toList(),
      store.events.map((event) => event.toJson()).toList(),
      store.stickyNotes,
      store.dayItemOverrides.map((day, names) => MapEntry('$day', names)),
    ]);
    if (connected && data == _lastAutoSyncData) return;
    _autoSyncRunning = true;
    final previousSync = store.lastSync;
    try {
      if (connected) {
        await sync(store);
      } else {
        await connectWifi(store);
      }
      if (store.lastSync != previousSync) _lastAutoSyncData = data;
    } finally {
      _autoSyncRunning = false;
    }
  }

  Future<String> _wifiRequest(String path, {String? body}) async {
    final uri = Uri.parse('$wifiBaseUrl$path');
    final request = body == null
        ? await _httpClient.getUrl(uri).timeout(const Duration(seconds: 5))
        : await _httpClient.postUrl(uri).timeout(const Duration(seconds: 5));
    if (body != null) {
      final payload = utf8.encode(body);
      request.headers.contentType = ContentType(
        'text',
        'plain',
        charset: 'utf-8',
      );
      // ESP32 WebServer reads Content-Length, not chunked request bodies.
      request.contentLength = payload.length;
      request.add(payload);
    }
    final response = await request.close().timeout(const Duration(seconds: 8));
    final text = await utf8.decoder.bind(response).join();
    if (response.statusCode != HttpStatus.ok) {
      throw HttpException('HTTP ${response.statusCode}: $text', uri: uri);
    }
    return text;
  }

  Future<void> connectWifi(BagStore store) async {
    status = '正在連線書包 Wi-Fi';
    notifyListeners();
    try {
      final response = await _wifiRequest('/status');
      if (!response.split(RegExp(r'[\r\n]+')).contains('READY\t1')) {
        throw const FormatException('書包回應格式不符，請確認裝置已開機');
      }
      wifiConnected = true;
      _handle(response, store);
      _startWifiStatusPolling(store);
      status = '已透過 Wi-Fi 連線';
      store.deviceStatus = status;
      notifyListeners();
      await _syncWifi(store);
    } catch (e) {
      wifiConnected = false;
      _wifiStatusTimer?.cancel();
      lastError = '$e';
      status = '等待書包連線';
      store.deviceStatus = status;
      notifyListeners();
      store.notifyListeners();
    }
  }

  void _startWifiStatusPolling(BagStore store) {
    _wifiStatusTimer?.cancel();
    _wifiStatusTimer = Timer.periodic(const Duration(seconds: 30), (_) async {
      if (!wifiConnected || _wifiStatusPollInFlight || _wifiSyncBusy) return;
      _wifiStatusPollInFlight = true;
      try {
        _handle(await _wifiRequest('/status'), store);
      } catch (e) {
        wifiConnected = false;
        _wifiStatusTimer?.cancel();
        status = 'Wi-Fi 已中斷：$e';
        store.deviceStatus = status;
        notifyListeners();
        store.notifyListeners();
      } finally {
        _wifiStatusPollInFlight = false;
      }
    });
  }

  Future<void> openWifiSettings() async {
    if (Platform.isAndroid) {
      await const MethodChannel(
        'smart_schoolbag/device',
      ).invokeMethod<void>('openWifiSettings');
    }
  }

  Future<T> _serialize<T>(Future<T> Function() operation) async {
    final previous = _commandTail;
    final release = Completer<void>();
    _commandTail = release.future;
    await previous;
    try {
      return await operation();
    } finally {
      release.complete();
    }
  }

  Future<void> scan() async {
    if (Platform.isAndroid) {
      final sdk =
          await const MethodChannel(
            'smart_schoolbag/device',
          ).invokeMethod<int>('androidSdkInt') ??
          31;
      final requested = sdk <= 30
          ? <Permission>[Permission.locationWhenInUse]
          : <Permission>[Permission.bluetoothScan, Permission.bluetoothConnect];
      final permissions = await requested.request();
      if (permissions.values.any((p) => !p.isGranted)) {
        status = '需要允許附近裝置權限才能搜尋 ESP32';
        notifyListeners();
        return;
      }
      if (sdk <= 30) {
        try {
          final locationService =
              await Permission.locationWhenInUse.serviceStatus;
          if (locationService.isDisabled) {
            status = '請開啟手機定位服務以搜尋 BLE 裝置（App 不讀取手機位置）';
            notifyListeners();
            return;
          }
        } catch (_) {}
      }
    }
    found.clear();
    scanning = true;
    status = '正在搜尋';
    notifyListeners();
    scanSub?.cancel();
    scanSub = FlutterBluePlus.scanResults.listen((results) {
      found
        ..clear()
        ..addAll(
          results.where(
            (r) =>
                r.device.platformName.toLowerCase().contains('schoolbag') ||
                r.advertisementData.advName.toLowerCase().contains(
                  'schoolbag',
                ) ||
                r.device.platformName.contains('書包'),
          ),
        );
      notifyListeners();
    });
    try {
      await FlutterBluePlus.startScan(timeout: const Duration(seconds: 12));
    } catch (e) {
      status = '掃描失敗：$e';
    }
    scanning = false;
    notifyListeners();
  }

  Future<void> connect(BluetoothDevice d, BagStore store) async {
    _userDisconnected = false;
    _reconnectTimer?.cancel();
    await disconnect(preserveIntent: true);
    device = d;
    status = '連線中';
    notifyListeners();
    try {
      await d.connect(
        timeout: const Duration(seconds: 12),
        license: License.nonprofit,
      );
      if (Platform.isAndroid) {
        try {
          await d.requestMtu(185);
        } catch (_) {}
      }
      connectionSub = d.connectionState.listen((s) {
        status = s == BluetoothConnectionState.connected ? '已連線' : '已離線';
        store.deviceStatus = status;
        if (s == BluetoothConnectionState.connected) _reconnectAttempts = 0;
        if (s == BluetoothConnectionState.disconnected) {
          control = null;
          if (!_userDisconnected && _reconnectAttempts < 5) {
            _reconnectAttempts++;
            _reconnectTimer = Timer(const Duration(seconds: 5), () {
              if (!_userDisconnected) unawaited(connect(d, store));
            });
          }
        }
        notifyListeners();
        store.notifyListeners();
      });
      final services = await d.discoverServices();
      final matches = services
          .where((s) => s.uuid.toString() == serviceUuid)
          .toList();
      final service = matches.isEmpty ? null : matches.first;
      if (service == null) throw StateError('韌體協定不符');
      for (final c in service.characteristics) {
        if (c.uuid.toString() == controlUuid) control = c;
        if (c.uuid.toString() == eventUuid) {
          await c.setNotifyValue(true);
          _eventBytes.clear();
          eventSub = c.onValueReceived.listen(
            (bytes) => _consumeEvents(bytes, store),
          );
        }
      }
      if (control == null) throw StateError('找不到控制通道');
      await _serialize(() async {
        await _send('GET');
        await Future<void>.delayed(const Duration(milliseconds: 250));
        await _syncNow(store);
      });
    } catch (e) {
      await disconnect(preserveIntent: true);
      status = '連線失敗：$e';
      store.deviceStatus = status;
      notifyListeners();
      store.notifyListeners();
    }
  }

  void _consumeEvents(List<int> bytes, BagStore store) {
    for (final byte in bytes) {
      if (byte == 10) {
        if (_eventBytes.isNotEmpty) {
          final line = utf8.decode(_eventBytes, allowMalformed: true);
          _eventBytes.clear();
          _handle(line, store);
        }
      } else if (_eventBytes.length < 512) {
        _eventBytes.add(byte);
      } else {
        // Drop a malformed/oversized event and resume at the next LF.
        _eventBytes.clear();
      }
    }
  }

  void _handle(String message, BagStore store) {
    for (final line
        in message.split(RegExp(r'[\r\n]+')).where((x) => x.isNotEmpty)) {
      lines.add(line);
      final p = line.split('\t');
      if (_legacyNotesReadback &&
          p.first != 'NOTE_ITEM' &&
          p.first != 'NOTE_COUNT' &&
          _notesReadbackExpected == 0) {
        _finishNotesReadback();
      }
      if (p.first == 'GPS' && p.length > 1) {
        if (p[1] == 'FIX' && p.length >= 5) {
          gps = '已定位 · 衛星 ${p[2]} · ${p[3]}, ${p[4]}';
          final latitude = double.tryParse(p[3]);
          final longitude = double.tryParse(p[4]);
          if (latitude != null &&
              longitude != null &&
              latitude >= -90 &&
              latitude <= 90 &&
              longitude >= -180 &&
              longitude <= 180) {
            gpsLatitude = latitude;
            gpsLongitude = longitude;
            unawaited(store.updateDeviceLocation(latitude, longitude));
          } else {
            gpsLatitude = null;
            gpsLongitude = null;
          }
        } else if (p[1] == 'NOFIX') {
          gps = '尚未定位 · 衛星 ${p.length > 2 ? p[2] : '0'}';
          gpsLatitude = null;
          gpsLongitude = null;
        } else {
          gps = 'GPS 狀態未知';
          gpsLatitude = null;
          gpsLongitude = null;
        }
        gpsUpdatedAt = DateTime.now();
        store.gpsStatus = gps;
      }
      if (p.first == 'VERSION' && p.length > 1) firmwareVersion = p[1];
      if (p.first == 'TAG' && p.length > 1) {
        rfid = p.skip(1).join(' ');
        store.lastRfid = rfid;
        if (p.length >= 3 && p[1] != 'UNKNOWN') {
          for (final item in store.items) {
            if (item.name == p[1]) {
              item.tag = p[2].toUpperCase();
              final weekday = p.length >= 4 ? int.tryParse(p[3]) : null;
              if (weekday != null && weekday >= 1 && weekday <= 7) {
                item.setCheckedOn(weekday, true);
              } else {
                item.checked = true;
              }
              unawaited(store.save());
              break;
            }
          }
        }
      }
      if (p.first == 'READY') {
        status = '已連線';
      }
      if (p.first == 'NOTES_BEGIN' && p.length >= 2) {
        _legacyNotesReadback = false;
        final count = int.tryParse(p[1]);
        if (count == null || count < 0 || count > 5) {
          final done = _notesReadbackDone;
          if (done != null && !done.isCompleted) {
            done.completeError(StateError('裝置回報的便利貼數量無效'));
          }
        } else {
          _notesReadbackExpected = count;
          _notesReadbackBuffer = List<String?>.filled(count, null);
        }
      }
      if (p.first == 'NOTE_COUNT' && p.length >= 2) {
        _legacyNotesReadback = true;
        final count = int.tryParse(p[1]);
        if (count == null || count < 0 || count > 5) {
          final done = _notesReadbackDone;
          if (done != null && !done.isCompleted) {
            done.completeError(StateError('裝置回報的便利貼數量無效'));
          }
        } else {
          _notesReadbackExpected = count;
          _notesReadbackBuffer = List<String?>.filled(count, null);
          if (count == 0) _finishNotesReadback();
        }
      }
      if (p.first == 'NOTE_ITEM' && p.length >= 3) {
        final index = int.tryParse(p[1]);
        final buffer = _notesReadbackBuffer;
        if (index != null &&
            buffer != null &&
            index >= 0 &&
            index < buffer.length) {
          buffer[index] = p.skip(2).join('\t');
          if (_legacyNotesReadback && buffer.every((note) => note != null)) {
            _finishNotesReadback();
          }
        }
      }
      if (p.first == 'NOTES_END') {
        _finishNotesReadback();
      }
      if (p.first == 'SYNC_OK') {
        lastError = '';
        store.resetToday = false;
        if (_syncDone != null && !_syncDone!.isCompleted) _syncDone!.complete();
        unawaited(store.save());
      }
      if (_payloadDone != null &&
          !_payloadDone!.isCompleted &&
          p.first == _payloadExpected) {
        _payloadDone!.complete();
      }
      if (p.first == 'ERR') {
        lastError = p.skip(1).join(' ');
        if (_notesReadbackDone != null && !_notesReadbackDone!.isCompleted) {
          _notesReadbackDone!.completeError(StateError(lastError));
        }
        if (_payloadDone != null && !_payloadDone!.isCompleted) {
          _payloadError = lastError;
          _payloadDone!.complete();
        }
        if (_syncDone != null && !_syncDone!.isCompleted) {
          _syncError = lastError;
          _syncDone!.complete();
        } else {
          status = '裝置錯誤：$lastError';
          store.deviceStatus = status;
        }
      }
      if (p.first == 'ITEM' && p.length >= 5) {
        final i = int.tryParse(p[1]);
        if (i != null && i >= 0 && i < store.items.length) {
          // The phone owns names and ordering. Adopt tag/check state only when
          // the device item still matches that phone-side index and name.
          if (store.items[i].name == p[2]) {
            store.items[i].tag = p[3] == '-' ? '' : p[3];
            final weekday = p.length >= 6 ? int.tryParse(p[5]) : null;
            if (weekday != null && weekday >= 1 && weekday <= 7) {
              store.items[i].setCheckedOn(weekday, p[4] == '1');
            } else if (!store.resetToday) {
              store.items[i].checked = p[4] == '1';
            }
            unawaited(store.save());
          }
        }
      }
    }
    notifyListeners();
    store.notifyListeners();
  }

  void _finishNotesReadback() {
    final expected = _notesReadbackExpected;
    final buffer = _notesReadbackBuffer;
    final done = _notesReadbackDone;
    if (expected == null ||
        buffer == null ||
        buffer.any((note) => note == null)) {
      if (done != null && !done.isCompleted) {
        done.completeError(StateError('裝置便利貼回讀資料不完整'));
      }
    } else {
      deviceStickyNotes = buffer.cast<String>();
      if (done != null && !done.isCompleted) done.complete();
    }
    _notesReadbackExpected = null;
    _notesReadbackBuffer = null;
    _legacyNotesReadback = false;
  }

  Future<void> _send(String line) async {
    final c = control;
    if (c == null) throw StateError('裝置未連線');
    final bytes = utf8.encode('$line\n');
    for (var offset = 0; offset < bytes.length; offset += 18) {
      final end = (offset + 18 < bytes.length) ? offset + 18 : bytes.length;
      await c.write(
        bytes.sublist(offset, end),
        withoutResponse: false,
        allowLongWrite: false,
      );
    }
  }

  Future<void> _sendExpecting(String line, String acknowledgement) async {
    _payloadExpected = acknowledgement;
    _payloadError = null;
    _payloadDone = Completer<void>();
    try {
      await _send(line);
      await _payloadDone!.future.timeout(const Duration(seconds: 5));
      if (_payloadError != null) throw StateError(_payloadError!);
    } finally {
      _payloadDone = null;
      _payloadExpected = null;
      _payloadError = null;
    }
  }

  Future<void> _verifyNotesWifi(List<String> expected, BagStore store) async {
    _notesReadbackDone = Completer<void>();
    _notesReadbackExpected = null;
    _notesReadbackBuffer = null;
    try {
      final response = await _wifiRequest('/status');
      _handle(response, store);
      await _notesReadbackDone!.future.timeout(const Duration(seconds: 5));
      if (!_sameNotes(expected, deviceStickyNotes)) {
        throw StateError('書包已回讀，但便利貼內容與手機不同');
      }
    } finally {
      _notesReadbackDone = null;
      _notesReadbackExpected = null;
      _notesReadbackBuffer = null;
    }
  }

  bool _sameNotes(List<String> a, List<String> b) =>
      a.length == b.length &&
      List<bool>.generate(a.length, (i) => a[i] == b[i]).every((x) => x);

  Future<void> _verifyNotesBle(List<String> expected) async {
    _notesReadbackDone = Completer<void>();
    _notesReadbackExpected = null;
    _notesReadbackBuffer = null;
    try {
      await _send('GET');
      await _notesReadbackDone!.future.timeout(const Duration(seconds: 5));
      if (!_sameNotes(expected, deviceStickyNotes)) {
        throw StateError('書包已回讀，但便利貼內容與手機不同');
      }
    } finally {
      _notesReadbackDone = null;
      _notesReadbackExpected = null;
      _notesReadbackBuffer = null;
    }
  }

  Future<void> sync(BagStore store) async {
    if (wifiConnected) return _syncWifi(store);
    if (control != null) return _serialize(() => _syncNow(store));
    status = '已保存在手機；連上書包後即可同步';
    notesSyncStatus = '尚未同步：已保存在手機';
    store.deviceStatus = status;
    notifyListeners();
    store.notifyListeners();
  }

  Future<void> _syncWifi(BagStore store) async {
    if (!wifiConnected || _wifiSyncBusy) return;
    _wifiSyncBusy = true;
    try {
      final snapshot = store.items
          .map((item) => BagItem.fromJson(item.toJson()))
          .toList(growable: false);
      final requiredNames = store
          .itemsFor(DateTime.now())
          .map((item) => item.name)
          .toSet();
      final commands = <String>['SYNC_BEGIN\t${snapshot.length.clamp(0, 20)}'];
      for (var i = 0; i < snapshot.length && i < 20; i++) {
        final item = snapshot[i];
        final name = item.name.replaceAll(RegExp(r'[\t\r\n]'), ' ');
        commands.add(
          'ITEM\t$i\t$name\t${item.tag.isEmpty ? '-' : item.tag}\t${item.checked ? 1 : 0}',
        );
        commands.add(
          'REQUIRED\t$i\t${requiredNames.contains(item.name) ? 1 : 0}',
        );
      }
      for (var i = 0; i < snapshot.length && i < 20; i++) {
        commands.add('CATEGORY\t$i\t${snapshot[i].category}');
      }
      commands.add('SYNC_DONE');
      commands.add(
        'TIME\t${DateTime.now().toUtc().millisecondsSinceEpoch ~/ 1000}',
      );
      commands.add('PLAN\t${_todayPlan(store)}');
      commands.addAll(_weeklyCommands(store));
      final notes = store.stickyNotes
          .take(5)
          .map(
            (note) => _cleanProtocolText(note.replaceAll('\n', '\u001e'), 120),
          )
          .toList(growable: false);
      commands.add('NOTE_BEGIN\t${notes.length}');
      for (var i = 0; i < notes.length; i++) {
        commands.add('NOTE_ITEM\t$i\t${_cleanProtocolText(notes[i], 120)}');
      }
      commands.add('NOTE_DONE');
      final response = await _wifiRequest(
        '/control',
        body: '${commands.join('\n')}\n',
      );
      _handle(response, store);
      if (response.contains('ERR\t')) throw StateError(response.trim());
      for (final acknowledgement in const [
        'SYNC_OK',
        'TIME_OK',
        'PLAN_OK',
        'NOTE_OK',
      ]) {
        if (!response.split(RegExp(r'[\r\n]+')).contains(acknowledgement)) {
          throw StateError('裝置未確認 $acknowledgement');
        }
      }
      await _verifyNotesWifi(notes, store);
      for (var weekday = 1; weekday <= 7; weekday++) {
        for (final prefix in const [
          'PLAN_DAY_OK',
          'DAYREQ_OK',
          'DAYCHECK_OK',
        ]) {
          if (!response
              .split(RegExp(r'[\r\n]+'))
              .contains('$prefix\t$weekday')) {
            throw StateError('裝置未確認星期 $weekday 的課表／清單資料');
          }
        }
      }
      store.lastSync = DateTime.now();
      status = 'Wi-Fi 已同步';
      notesSyncStatus = '便利貼已回讀確認';
      store.deviceStatus = status;
      await store.save();
    } catch (e) {
      lastError = '$e';
      status = '等待自動重試';
      notesSyncStatus = '等待自動重試';
      store.deviceStatus = status;
    } finally {
      _wifiSyncBusy = false;
    }
    notifyListeners();
    store.notifyListeners();
  }

  String _todayPlan(BagStore store) {
    final today = DateTime.now();
    final classes = store
        .lessonsFor(today)
        .map((lesson) => '${lesson.period}.${lesson.subject}')
        .toList();
    final events = store.events
        .where((event) => occursOnDate(event, today))
        .map((event) => event.title);
    final result = <String>[];
    var byteCount = 0;
    for (final entry in [...classes, ...events]) {
      final clean = entry.replaceAll('|', ' ');
      final extra = utf8.encode(clean).length + (result.isEmpty ? 0 : 1);
      if (byteCount + extra > 150) break;
      result.add(clean);
      byteCount += extra;
    }
    return result.isEmpty ? '今日沒有課程或行事' : result.join('|');
  }

  List<String> _weeklyCommands(BagStore store) {
    final commands = <String>[];
    for (var weekday = 1; weekday <= 7; weekday++) {
      commands.addAll(_weeklyCommandsForDay(store, weekday));
    }
    return commands;
  }

  List<String> _weeklyCommandsForDay(BagStore store, int weekday) {
    final now = DateTime.now();
    final today = DateTime(now.year, now.month, now.day);
    final date = today.add(Duration(days: (weekday - today.weekday + 7) % 7));
    final entries = <String>[
      ...store
          .lessonsFor(date)
          .map((lesson) => '${lesson.period}.${lesson.subject}'),
      ...store.events
          .where((event) => occursOnDate(event, date))
          .map((event) => '行.${event.title}'),
    ];
    final plan = _cleanProtocolText(
      entries.map((entry) => entry.replaceAll('|', ' ')).join('|'),
      150,
    );
    final requiredNames = store.itemsFor(date).map((item) => item.name).toSet();
    var requiredMask = 0;
    var checkedMask = 0;
    for (var index = 0; index < store.items.length && index < 20; index++) {
      final item = store.items[index];
      if (requiredNames.contains(item.name)) requiredMask |= 1 << index;
      if (item.checkedOn(weekday)) checkedMask |= 1 << index;
    }
    return [
      'PLAN_DAY\t$weekday\t${plan.isEmpty ? '今日無課程' : plan}',
      'DAYREQ\t$weekday\t${requiredMask.toRadixString(16)}',
      'DAYCHECK\t$weekday\t${checkedMask.toRadixString(16)}',
    ];
  }

  String _cleanProtocolText(String value, int maxBytes) {
    final clean = value.replaceAll(RegExp(r'[\t\r\n]'), ' ').trim();
    final output = StringBuffer();
    var bytes = 0;
    for (final rune in clean.runes) {
      final char = String.fromCharCode(rune);
      final size = utf8.encode(char).length;
      if (bytes + size > maxBytes) break;
      output.write(char);
      bytes += size;
    }
    return output.toString();
  }

  Future<void> _syncNow(BagStore store) async {
    if (control == null) return;
    try {
      _syncDone = Completer<void>();
      _syncError = null;
      // Keep the count and every ITEM/REQUIRED pair from one consistent view
      // even if the user edits the list while BLE writes are in flight.
      final items = store.items
          .map((item) => BagItem.fromJson(item.toJson()))
          .toList(growable: false);
      final requiredNames = store
          .itemsFor(DateTime.now())
          .map((e) => e.name)
          .toSet();
      await _send('SYNC_BEGIN\t${items.length.clamp(0, 20)}');
      for (var i = 0; i < items.length && i < 20; i++) {
        final x = items[i];
        await _send(
          'ITEM\t$i\t${x.name.replaceAll('\t', ' ')}\t${x.tag.isEmpty ? '-' : x.tag}\t${x.checked ? 1 : 0}',
        );
        await _send('REQUIRED\t$i\t${requiredNames.contains(x.name) ? 1 : 0}');
      }
      for (var i = 0; i < items.length && i < 20; i++) {
        await _send('CATEGORY\t$i\t${items[i].category}');
      }
      await _send('SYNC_DONE');
      await _syncDone!.future.timeout(const Duration(seconds: 5));
      if (_syncError != null) throw StateError(_syncError!);
      await _sendExpecting(
        'TIME\t${DateTime.now().toUtc().millisecondsSinceEpoch ~/ 1000}',
        'TIME_OK',
      );
      await _sendExpecting('PLAN\t${_todayPlan(store)}', 'PLAN_OK');
      for (var weekday = 1; weekday <= 7; weekday++) {
        final commands = _weeklyCommandsForDay(store, weekday);
        await _sendExpecting(commands[0], 'PLAN_DAY_OK');
        await _sendExpecting(commands[1], 'DAYREQ_OK');
        await _sendExpecting(commands[2], 'DAYCHECK_OK');
      }
      final notes = store.stickyNotes
          .take(5)
          .map(
            (note) => _cleanProtocolText(note.replaceAll('\n', '\u001e'), 120),
          )
          .toList(growable: false);
      await _send('NOTE_BEGIN\t${notes.length}');
      for (var i = 0; i < notes.length; i++) {
        await _send('NOTE_ITEM\t$i\t${_cleanProtocolText(notes[i], 120)}');
      }
      await _sendExpecting('NOTE_DONE', 'NOTE_OK');
      await _verifyNotesBle(notes);
      store.lastSync = DateTime.now();
      status = '已同步';
      notesSyncStatus = '便利貼已回讀確認';
      notifyListeners();
      store.deviceStatus = status;
      await store.save();
    } catch (e) {
      status = '同步失敗：$e';
      notesSyncStatus = '便利貼同步待處理：$e';
      notifyListeners();
      store.deviceStatus = status;
      store.notifyListeners();
    } finally {
      _syncDone = null;
      _syncError = null;
    }
  }

  Future<void> _sendQueuedCommand(String line) => _serialize(() async {
    try {
      await _send(line);
    } catch (e) {
      lastError = '$e';
      status = '命令傳送失敗';
      notifyListeners();
    }
  });

  Future<void> pairItem(int index, BagStore store) async {
    await autoSync(store);
    if (!connected) {
      status = '請先連上書包';
      notifyListeners();
      return;
    }
    _pairingInProgress = true;
    final oldTag = store.items[index].tag;
    status = '請將 RFID 靠近讀卡器';
    notifyListeners();
    try {
      if (wifiConnected) {
        final reply = await _wifiRequest('/control', body: 'PAIR\t$index\n');
        if (!reply.contains('PAIR_READY')) throw StateError(reply);
        for (var attempt = 0; attempt < 60; attempt++) {
          await Future<void>.delayed(const Duration(seconds: 1));
          _handle(await _wifiRequest('/status'), store);
          if (store.items[index].tag.isNotEmpty &&
              store.items[index].tag != oldTag) {
            status = 'RFID 已配對';
            return;
          }
        }
        status = '配對等待結束，可重新掃描';
      } else {
        await _send('PAIR\t$index');
      }
    } catch (error) {
      lastError = '$error';
      status = '配對未完成';
    } finally {
      _pairingInProgress = false;
      notifyListeners();
    }
  }

  Future<void> pair(int index) => _sendQueuedCommand('PAIR\t$index');
  Future<void> unpair(int index) => _sendQueuedCommand('UNPAIR\t$index');
  Future<void> setChecked(
    int index,
    bool checked,
    BagStore store, {
    int? weekday,
  }) async {
    final line = weekday == null
        ? 'CHECK\t$index\t${checked ? 1 : 0}\n'
        : 'CHECKDAY\t$weekday\t$index\t${checked ? 1 : 0}\n';
    if (wifiConnected) {
      try {
        _handle(await _wifiRequest('/control', body: line), store);
      } catch (e) {
        status = '勾選同步失敗：$e';
        notifyListeners();
      }
    } else {
      await _sendQueuedCommand(line.trimRight());
    }
  }

  Future<void> disconnect({bool preserveIntent = false}) async {
    if (!preserveIntent) _userDisconnected = true;
    _reconnectTimer?.cancel();
    _wifiStatusTimer?.cancel();
    await eventSub?.cancel();
    await connectionSub?.cancel();
    final d = device;
    device = null;
    control = null;
    wifiConnected = false;
    if (d != null) {
      try {
        await d.disconnect();
      } catch (_) {}
    }
    status = '已離線';
    notifyListeners();
  }

  @override
  void dispose() {
    _userDisconnected = true;
    _reconnectTimer?.cancel();
    _wifiStatusTimer?.cancel();
    scanSub?.cancel();
    connectionSub?.cancel();
    eventSub?.cancel();
    _httpClient.close(force: true);
    lines.close();
    super.dispose();
  }
}

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await intl_data.initializeDateFormatting('zh_TW');
  await ReminderService.init();
  await ReminderService.migrateNotificationIds();
  final store = BagStore();
  await store.load();
  await store
      .save(); // persist generated calendar UIDs for notification identity
  await ReminderService.scheduleAll(store.events);
  runApp(SchoolbagApp(store: store));
}

class SchoolbagApp extends StatefulWidget {
  const SchoolbagApp({super.key, required this.store});
  final BagStore store;
  @override
  State<SchoolbagApp> createState() => _SchoolbagAppState();
}

class _SchoolbagAppState extends State<SchoolbagApp>
    with WidgetsBindingObserver {
  final ble = BagBle();
  bool _refreshingReminders = false;
  Timer? _reminderRefreshTimer;
  Timer? _autoSyncTimer;
  Timer? _syncDebounce;
  void _queueAutoSync() {
    _syncDebounce?.cancel();
    _syncDebounce = Timer(
      const Duration(milliseconds: 800),
      () => unawaited(ble.autoSync(widget.store)),
    );
  }

  Future<void> _refreshReminders() async {
    if (_refreshingReminders) return;
    _refreshingReminders = true;
    try {
      await ReminderService.scheduleAll(widget.store.events);
    } finally {
      _refreshingReminders = false;
    }
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state == AppLifecycleState.resumed) {
      unawaited(_refreshReminders());
      _queueAutoSync();
    }
  }

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    widget.store.addListener(_queueAutoSync);
    _queueAutoSync();
    _autoSyncTimer = Timer.periodic(
      const Duration(seconds: 10),
      (_) => unawaited(ble.autoSync(widget.store)),
    );
    _reminderRefreshTimer = Timer.periodic(
      const Duration(hours: 12),
      (_) => unawaited(_refreshReminders()),
    );
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    _reminderRefreshTimer?.cancel();
    widget.store.removeListener(_queueAutoSync);
    _autoSyncTimer?.cancel();
    _syncDebounce?.cancel();
    ble.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => MaterialApp(
    title: appTitle,
    debugShowCheckedModeBanner: false,
    locale: const Locale('zh', 'TW'),
    supportedLocales: const [Locale('zh', 'TW')],
    localizationsDelegates: const [
      GlobalMaterialLocalizations.delegate,
      GlobalWidgetsLocalizations.delegate,
      GlobalCupertinoLocalizations.delegate,
    ],
    theme: ThemeData(
      colorScheme: ColorScheme.fromSeed(
        seedColor: const Color(0xff0b7772),
        brightness: Brightness.light,
      ),
      useMaterial3: true,
      scaffoldBackgroundColor: Colors.white,
      appBarTheme: const AppBarTheme(
        backgroundColor: Colors.white,
        surfaceTintColor: Colors.transparent,
        scrolledUnderElevation: 0,
        titleTextStyle: TextStyle(
          color: Color(0xff172522),
          fontSize: 19,
          fontWeight: FontWeight.w700,
        ),
      ),
      cardTheme: CardThemeData(
        color: Colors.white,
        elevation: 0,
        margin: const EdgeInsets.symmetric(vertical: 4),
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(10),
          side: const BorderSide(color: Color(0xffe5eae8)),
        ),
      ),
      navigationBarTheme: const NavigationBarThemeData(
        height: 62,
        backgroundColor: Colors.white,
        indicatorColor: Color(0xffe4f2ef),
        elevation: 0,
      ),
      inputDecorationTheme: InputDecorationTheme(
        filled: true,
        fillColor: const Color(0xfff7f9f8),
        border: OutlineInputBorder(
          borderRadius: BorderRadius.all(Radius.circular(8)),
          borderSide: BorderSide(color: Color(0xffdce4e1)),
        ),
      ),
    ),
    home: HomeShell(store: widget.store, ble: ble),
  );
}

class HomeShell extends StatefulWidget {
  const HomeShell({super.key, required this.store, required this.ble});
  final BagStore store;
  final BagBle ble;
  @override
  State<HomeShell> createState() => _HomeShellState();
}

class _HomeShellState extends State<HomeShell> {
  int tab = 0;
  Timer? _dateTimer;
  DateTime _observedDate = DateTime.now();

  @override
  void initState() {
    super.initState();
    _dateTimer = Timer.periodic(const Duration(seconds: 30), (_) {
      unawaited(_resetChecklistOnDateChange());
    });
  }

  Future<void> _resetChecklistOnDateChange() async {
    final now = DateTime.now();
    if (DateUtils.isSameDay(_observedDate, now)) return;
    _observedDate = DateTime(now.year, now.month, now.day);
    for (final item in widget.store.items) {
      item.checked = false;
    }
    widget.store.resetToday = true;
    await widget.store.save();
    if (widget.ble.connected) await widget.ble.sync(widget.store);
  }

  @override
  void dispose() {
    _dateTimer?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => AnimatedBuilder(
    animation: Listenable.merge([widget.store, widget.ble]),
    builder: (context, _) {
      final isMoreDetail = tab >= 4;
      final page = switch (tab) {
        0 => TodayPage(store: widget.store, ble: widget.ble),
        1 => TimetablePage(store: widget.store, ble: widget.ble),
        2 => JournalPage(
          onOpen: (destination) => setState(() => tab = destination),
        ),
        3 => MorePage(
          onOpen: (destination) => setState(() => tab = destination),
        ),
        4 => NotesPage(store: widget.store, ble: widget.ble),
        5 => ItemsPage(store: widget.store, ble: widget.ble),
        6 => DevicePage(store: widget.store, ble: widget.ble),
        8 => CalendarPage(store: widget.store, ble: widget.ble),
        _ => GpsMapPage(ble: widget.ble),
      };
      final titles = <int, String>{
        0: '今日要帶',
        1: '我的課表',
        2: '記事',
        3: '更多',
        4: '便利貼',
        5: '物品與標籤',
        6: '裝置連線',
        7: 'GPS 位置',
        8: '行事曆',
      };
      return Scaffold(
        appBar: AppBar(
          leading: isMoreDetail
              ? IconButton(
                  tooltip: '返回更多功能',
                  icon: const Icon(Icons.arrow_back_rounded),
                  onPressed: () =>
                      setState(() => tab = (tab == 4 || tab == 8) ? 2 : 3),
                )
              : null,
          title: Text(titles[tab] ?? appTitle),
          actions: const [],
        ),
        body: AnimatedSwitcher(
          duration: const Duration(milliseconds: 180),
          child: KeyedSubtree(key: ValueKey(tab), child: page),
        ),
        bottomNavigationBar: NavigationBar(
          selectedIndex: (tab == 4 || tab == 8) ? 2 : (tab < 3 ? tab : 3),
          onDestinationSelected: (i) => setState(() => tab = i),
          destinations: const [
            NavigationDestination(
              icon: Icon(Icons.checklist_rounded),
              selectedIcon: Icon(Icons.checklist),
              label: '今日',
            ),
            NavigationDestination(
              icon: Icon(Icons.menu_book_outlined),
              selectedIcon: Icon(Icons.menu_book_rounded),
              label: '課表',
            ),
            NavigationDestination(
              icon: Icon(Icons.event_outlined),
              selectedIcon: Icon(Icons.event),
              label: '記事',
            ),
            NavigationDestination(
              icon: Icon(Icons.grid_view_rounded),
              selectedIcon: Icon(Icons.grid_view_rounded),
              label: '更多',
            ),
          ],
        ),
      );
    },
  );
}

class JournalPage extends StatelessWidget {
  const JournalPage({super.key, required this.onOpen});
  final ValueChanged<int> onOpen;
  @override
  Widget build(BuildContext context) => ListView(
    padding: const EdgeInsets.all(20),
    children: [
      _MoreFeatureRow(
        icon: Icons.sticky_note_2_outlined,
        title: '便利貼',
        subtitle: '隨手記事',
        onTap: () => onOpen(4),
      ),
      const Divider(height: 1),
      _MoreFeatureRow(
        icon: Icons.event_outlined,
        title: '行事曆',
        subtitle: '日期與提醒',
        onTap: () => onOpen(8),
      ),
    ],
  );
}

class MorePage extends StatelessWidget {
  const MorePage({super.key, required this.onOpen});
  final ValueChanged<int> onOpen;

  @override
  Widget build(BuildContext context) => ListView(
    padding: const EdgeInsets.fromLTRB(20, 12, 20, 24),
    children: [
      _MoreFeatureRow(
        icon: Icons.sell_outlined,
        title: '物品與 RFID',
        subtitle: '自訂課本、用品名稱與標籤配對',
        onTap: () => onOpen(5),
      ),
      const Divider(height: 1),
      _MoreFeatureRow(
        icon: Icons.wifi_outlined,
        title: '裝置連線',
        subtitle: '連接書包、同步課表與查看韌體',
        onTap: () => onOpen(6),
      ),
      const Divider(height: 1),
      _MoreFeatureRow(
        icon: Icons.map_outlined,
        title: 'GPS 位置地圖',
        subtitle: '查看書包座標並開啟地圖 App',
        onTap: () => onOpen(7),
      ),
      const Divider(height: 1),
      const Padding(
        padding: EdgeInsets.only(top: 16),
        child: Text(
          '課表、行事與物品清單會整理成今日清單。RFID 標籤直接對應物品名稱。',
          style: TextStyle(height: 1.45, color: Color(0xff687570)),
        ),
      ),
    ],
  );
}

class _MoreFeatureRow extends StatelessWidget {
  const _MoreFeatureRow({
    required this.icon,
    required this.title,
    required this.subtitle,
    required this.onTap,
  });

  final IconData icon;
  final String title;
  final String subtitle;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => ListTile(
    contentPadding: EdgeInsets.zero,
    minLeadingWidth: 28,
    leading: Icon(icon, color: Theme.of(context).colorScheme.primary),
    title: Text(title, style: const TextStyle(fontWeight: FontWeight.w600)),
    subtitle: Text(subtitle),
    trailing: const Icon(Icons.chevron_right_rounded, size: 20),
    onTap: onTap,
  );
}

class TodayPage extends StatefulWidget {
  const TodayPage({super.key, required this.store, required this.ble});
  final BagStore store;
  final BagBle ble;

  @override
  State<TodayPage> createState() => _TodayPageState();
}

class _TodayPageState extends State<TodayPage> {
  late int weekday = DateTime.now().weekday;

  @override
  Widget build(BuildContext context) {
    final now = DateTime.now();
    final monday = DateTime(
      now.year,
      now.month,
      now.day,
    ).subtract(Duration(days: now.weekday - 1));
    final selectedDate = monday.add(Duration(days: weekday - 1));
    final lessons = widget.store.lessonsFor(selectedDate);
    final items = widget.store.itemsFor(selectedDate);
    final todayEvents =
        widget.store.events
            .where((event) => occursOnDate(event, selectedDate))
            .toList()
          ..sort((a, b) => a.date.compareTo(b.date));
    final done = items.where((i) => i.checkedOn(weekday)).length;
    return ListView(
      padding: const EdgeInsets.fromLTRB(20, 12, 20, 24),
      children: [
        Text(
          DateFormat('M月d日 EEEE', 'zh_TW').format(selectedDate),
          style: const TextStyle(
            color: Color(0xff53615d),
            fontSize: 14,
            fontWeight: FontWeight.w500,
          ),
        ),
        const SizedBox(height: 10),
        DropdownButtonFormField<int>(
          initialValue: weekday,
          decoration: const InputDecoration(
            labelText: '選擇星期的清單',
            border: OutlineInputBorder(),
            isDense: true,
          ),
          items: List.generate(
            7,
            (index) => DropdownMenuItem(
              value: index + 1,
              child: Text(
                '星期${const ['一', '二', '三', '四', '五', '六', '日'][index]}要帶',
              ),
            ),
          ),
          onChanged: (value) => setState(() => weekday = value ?? weekday),
        ),
        TextButton.icon(
          onPressed: () => chooseDayItems(context, widget.store, weekday),
          icon: const Icon(Icons.edit_outlined),
          label: const Text('從物品資料庫選擇'),
        ),
        const SizedBox(height: 12),
        const Text(
          '要帶的物品',
          style: TextStyle(
            color: Color(0xff172522),
            fontSize: 22,
            fontWeight: FontWeight.w700,
          ),
        ),
        const SizedBox(height: 5),
        Text(
          '星期${const ['一', '二', '三', '四', '五', '六', '日'][weekday - 1]}　·　課程 ${lessons.length} 節　·　行事 ${todayEvents.length} 項',
          style: const TextStyle(color: Color(0xff687570), fontSize: 13),
        ),
        const SizedBox(height: 14),
        LinearProgressIndicator(
          minHeight: 3,
          value: items.isEmpty ? 0 : done / items.length,
          backgroundColor: const Color(0xffe8eeeb),
          valueColor: const AlwaysStoppedAnimation<Color>(Color(0xff0b7772)),
        ),
        const SizedBox(height: 7),
        Text(
          items.isEmpty ? '尚未設定今日物品' : '已確認 $done / ${items.length} 項',
          style: const TextStyle(color: Color(0xff687570), fontSize: 12),
        ),
        if (items.isEmpty)
          const _Empty(text: '這天尚無物品；從「物品與 RFID」加入書包物品，再設定星期或課表。'),
        ...items.map((item) {
          final i = widget.store.items.indexOf(item);
          return CheckboxListTile(
            shape: const RoundedRectangleBorder(),
            contentPadding: EdgeInsets.zero,
            dense: true,
            value: item.checkedOn(weekday),
            title: Text(item.name),
            subtitle: Text(
              item.tag.isEmpty ? '尚未配對 RFID' : '已配對 · ${item.tag}',
            ),
            onChanged: (v) async {
              item.setCheckedOn(weekday, v ?? false);
              await widget.store.save();
              if (i >= 0 && widget.ble.connected) {
                await widget.ble.setChecked(
                  i,
                  item.checkedOn(weekday),
                  widget.store,
                  weekday: weekday,
                );
              }
            },
            secondary: Icon(
              item.tag.isEmpty ? Icons.sell_outlined : Icons.nfc,
              size: 19,
              color: const Color(0xff687570),
            ),
          );
        }),
        if (lessons.isNotEmpty) ...[
          const _SectionTitle('課表'),
          ...lessons.map(
            (lesson) => ListTile(
              dense: true,
              contentPadding: EdgeInsets.zero,
              leading: SizedBox(width: 30, child: Text('${lesson.period}')),
              title: Text(lesson.subject),
              subtitle: Text('要帶：${lesson.itemNames.join('、')}'),
            ),
          ),
        ],
        if (todayEvents.isNotEmpty) ...[
          const _SectionTitle('行事'),
          ...todayEvents.map(
            (event) => ListTile(
              dense: true,
              contentPadding: EdgeInsets.zero,
              leading: const Icon(Icons.event_note_outlined),
              title: Text(event.title),
              subtitle: Text(event.description),
            ),
          ),
        ],
        const Divider(height: 1),
        Card(
          color: const Color(0xfff1f6f3),
          child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 9),
            child: Row(
              children: [
                Icon(
                  widget.ble.connected
                      ? Icons.wifi_rounded
                      : Icons.wifi_off_rounded,
                  color: widget.ble.connected
                      ? const Color(0xff0b7772)
                      : const Color(0xff7b8882),
                ),
                const SizedBox(width: 10),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(widget.ble.connected ? '書包已連線' : '資料已保存在手機'),
                      Text(
                        '最後同步：${widget.store.lastSync == null ? '尚無' : DateFormat('M/d HH:mm').format(widget.store.lastSync!)}',
                        style: Theme.of(context).textTheme.bodySmall,
                      ),
                    ],
                  ),
                ),
                FilledButton.tonalIcon(
                  onPressed: () => widget.ble.connected
                      ? widget.ble.sync(widget.store)
                      : widget.ble.connectWifi(widget.store),
                  icon: const Icon(Icons.sync_rounded),
                  label: Text(widget.ble.connected ? '同步' : '連線同步'),
                ),
              ],
            ),
          ),
        ),
        if (widget.ble.rfid.isNotEmpty)
          ListTile(
            contentPadding: EdgeInsets.zero,
            leading: const Icon(Icons.nfc, size: 19),
            title: const Text('最近刷卡'),
            subtitle: Text(widget.ble.rfid),
          ),
      ],
    );
  }
}

class TimetablePage extends StatefulWidget {
  const TimetablePage({super.key, required this.store, required this.ble});
  final BagStore store;
  final BagBle ble;
  @override
  State<TimetablePage> createState() => _TimetablePageState();
}

class _TimetablePageState extends State<TimetablePage> {
  int day = DateTime.now().weekday;
  @override
  Widget build(BuildContext context) {
    final rows = widget.store.lessons.where((l) => l.weekday == day).toList()
      ..sort((a, b) => a.period.compareTo(b.period));
    return Column(
      children: [
        Padding(
          padding: const EdgeInsets.fromLTRB(16, 8, 16, 4),
          child: DropdownButtonFormField<int>(
            initialValue: day,
            decoration: const InputDecoration(
              labelText: '選擇星期',
              border: OutlineInputBorder(),
              isDense: true,
            ),
            items: List.generate(
              7,
              (i) => DropdownMenuItem(
                value: i + 1,
                child: Text(
                  '星期${const ['一', '二', '三', '四', '五', '六', '日'][i]}',
                ),
              ),
            ),
            onChanged: (value) => setState(() => day = value ?? day),
          ),
        ),
        TextButton.icon(
          onPressed: _copyDay,
          icon: const Icon(Icons.content_copy),
          label: const Text('複製整日到其他星期'),
        ),
        Expanded(
          child: ListView.builder(
            itemCount: 12,
            itemBuilder: (context, index) {
              final period = index + 1;
              final lesson = rows
                  .where((entry) => entry.period == period)
                  .firstOrNull;
              return ListTile(
                dense: true,
                leading: SizedBox(
                  width: 38,
                  child: Text(
                    '$period節',
                    style: const TextStyle(fontWeight: FontWeight.w700),
                  ),
                ),
                title: Text(lesson?.subject ?? '—'),
                subtitle: lesson == null || lesson.itemNames.isEmpty
                    ? null
                    : Text('要帶：${lesson.itemNames.join('、')}'),
                onTap: () => _editLesson(lesson, initialPeriod: period),
                trailing: lesson == null
                    ? const Icon(Icons.add, size: 18)
                    : IconButton(
                        tooltip: '刪除第 $period 節',
                        icon: const Icon(Icons.delete_outline),
                        onPressed: () async {
                          widget.store.lessons.remove(lesson);
                          await widget.store.save();
                          await widget.ble.sync(widget.store);
                        },
                      ),
              );
            },
          ),
        ),
        Padding(
          padding: const EdgeInsets.all(12),
          child: FilledButton.tonalIcon(
            onPressed: () => widget.ble.connected
                ? widget.ble.sync(widget.store)
                : widget.ble.connectWifi(widget.store),
            icon: const Icon(Icons.sync),
            label: const Text('同步完整週課表'),
          ),
        ),
      ],
    );
  }

  Future<void> _copyDay() async {
    final source = widget.store.lessons
        .where((lesson) => lesson.weekday == day)
        .toList();
    if (source.isEmpty) {
      ScaffoldMessenger.of(
        context,
      ).showSnackBar(const SnackBar(content: Text('來源日沒有課程可複製。')));
      return;
    }
    var targetDay = day == 7 ? 1 : day + 1;
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (ctx) => StatefulBuilder(
        builder: (ctx, setD) => AlertDialog(
          title: const Text('複製整日課表'),
          content: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text('將星期${['一', '二', '三', '四', '五', '六', '日'][day - 1]}的課程複製到：'),
              DropdownButton<int>(
                value: targetDay,
                items: List.generate(7, (i) => i + 1)
                    .where((d) => d != day)
                    .map(
                      (d) => DropdownMenuItem(
                        value: d,
                        child: Text(
                          '星期${['一', '二', '三', '四', '五', '六', '日'][d - 1]}',
                        ),
                      ),
                    )
                    .toList(),
                onChanged: (v) => setD(() => targetDay = v ?? targetDay),
              ),
              const Text('目標日原有課程會由複製內容取代。'),
            ],
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(ctx, false),
              child: const Text('取消'),
            ),
            FilledButton(
              onPressed: () => Navigator.pop(ctx, true),
              child: const Text('覆蓋並複製'),
            ),
          ],
        ),
      ),
    );
    if (confirmed != true || !mounted) return;
    widget.store.lessons.removeWhere((lesson) => lesson.weekday == targetDay);
    widget.store.lessons.addAll(
      source.map(
        (lesson) => Lesson(
          weekday: targetDay,
          period: lesson.period,
          subject: lesson.subject,
          itemNames: List<String>.from(lesson.itemNames),
        ),
      ),
    );
    await widget.store.save();
    if (widget.ble.connected) await widget.ble.sync(widget.store);
    if (mounted) setState(() {});
  }

  Future<void> _editLesson(Lesson? old, {int initialPeriod = 1}) async {
    final subject = TextEditingController(text: old?.subject ?? '');
    final period = TextEditingController(
      text: '${old?.period ?? initialPeriod}',
    );
    final selected = Set<String>.from(old?.itemNames ?? []);
    var weekday = old?.weekday ?? day;
    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => StatefulBuilder(
        builder: (ctx, setD) => AlertDialog(
          title: Text(old == null ? '新增課程' : '編輯課程'),
          content: SingleChildScrollView(
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                TextField(
                  controller: subject,
                  decoration: const InputDecoration(labelText: '課程名稱'),
                ),
                TextField(
                  controller: period,
                  keyboardType: TextInputType.number,
                  decoration: const InputDecoration(labelText: '節次'),
                ),
                DropdownButton<int>(
                  value: weekday,
                  items: List.generate(
                    7,
                    (i) => DropdownMenuItem(
                      value: i + 1,
                      child: Text(
                        '星期${['一', '二', '三', '四', '五', '六', '日'][i]}',
                      ),
                    ),
                  ),
                  onChanged: (v) => setD(() => weekday = v ?? weekday),
                ),
                for (final item in widget.store.items)
                  CheckboxListTile(
                    dense: true,
                    value: selected.contains(item.name),
                    title: Text(item.name),
                    onChanged: (v) => setD(
                      () => v == true
                          ? selected.add(item.name)
                          : selected.remove(item.name),
                    ),
                  ),
              ],
            ),
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(ctx, false),
              child: const Text('取消'),
            ),
            FilledButton(
              onPressed: () => Navigator.pop(ctx, true),
              child: const Text('儲存'),
            ),
          ],
        ),
      ),
    );
    if (!mounted) return;
    final periodNumber = int.tryParse(period.text);
    if (ok == true && subject.text.trim().isNotEmpty) {
      if (periodNumber == null || periodNumber < 1 || periodNumber > 12) {
        ScaffoldMessenger.of(
          context,
        ).showSnackBar(const SnackBar(content: Text('節次請輸入 1 到 12。')));
        return;
      }
      if (old != null) widget.store.lessons.remove(old);
      widget.store.lessons.add(
        Lesson(
          weekday: weekday,
          period: periodNumber,
          subject: subject.text.trim(),
          itemNames: selected.toList(),
        ),
      );
      await widget.store.save();
      if (!mounted) return;
      if (widget.ble.connected) await widget.ble.sync(widget.store);
    }
  }
}

class CalendarPage extends StatefulWidget {
  const CalendarPage({super.key, required this.store, required this.ble});
  final BagStore store;
  final BagBle ble;
  @override
  State<CalendarPage> createState() => _CalendarPageState();
}

class _CalendarPageState extends State<CalendarPage> {
  @override
  Widget build(BuildContext context) {
    final entries = [...widget.store.events]
      ..sort((a, b) => a.date.compareTo(b.date));
    return Column(
      children: [
        Expanded(
          child: entries.isEmpty
              ? const _Empty(text: '選個日期，記下要做的事。')
              : ListView(
                  children: [
                    for (var i = 0; i < entries.length; i++)
                      ListTile(
                        leading: const Icon(Icons.event_note),
                        title: Text(entries[i].title),
                        subtitle: Text(
                          '${DateFormat('M月d日 EEEE HH:mm', 'zh_TW').format(entries[i].date)}${entries[i].rrule.isEmpty ? '' : ' · 重複'}${entries[i].description.isEmpty ? '' : ' · ${entries[i].description}'}',
                        ),
                        onTap: () => _edit(entries[i]),
                        trailing: IconButton(
                          icon: const Icon(Icons.delete_outline),
                          onPressed: () async {
                            await ReminderService.cancel(
                              entries[i],
                              _stableId(entries[i]),
                            );
                            widget.store.events.remove(entries[i]);
                            await widget.store.save();
                            await ReminderService.scheduleAll(
                              widget.store.events,
                            );
                            if (widget.ble.connected) {
                              await widget.ble.sync(widget.store);
                            }
                          },
                        ),
                      ),
                  ],
                ),
        ),
        Wrap(
          alignment: WrapAlignment.center,
          spacing: 8,
          children: [
            FilledButton.icon(
              onPressed: () => _edit(null),
              icon: const Icon(Icons.add),
              label: const Text('新增行事'),
            ),
          ],
        ),
        const SizedBox(height: 12),
      ],
    );
  }

  Future<void> _edit(CalendarEntry? old) async {
    final title = TextEditingController(text: old?.title ?? ''),
        desc = TextEditingController(text: old?.description ?? '');
    var date = old?.date ?? DateTime.now().add(const Duration(days: 1));
    bool remind = old?.remind ?? true;
    String recurrence = old?.rrule ?? '';
    final recurrenceOptions = <(String, String)>[
      ('', '不重複'),
      ('FREQ=DAILY', '每天'),
      ('FREQ=WEEKLY', '每週'),
      ('FREQ=MONTHLY', '每月'),
    ];
    if (!recurrenceOptions.any((option) => option.$1 == recurrence)) {
      recurrenceOptions.add((recurrence, '保留目前重複規則'));
    }
    final result = await showDialog<bool>(
      context: context,
      builder: (ctx) => StatefulBuilder(
        builder: (ctx, setD) => AlertDialog(
          title: Text(old == null ? '新增行事' : '編輯行事'),
          content: SingleChildScrollView(
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                TextField(
                  controller: title,
                  decoration: const InputDecoration(labelText: '要做什麼事'),
                ),
                TextField(
                  controller: desc,
                  decoration: const InputDecoration(labelText: '備註（選填）'),
                ),
                ListTile(
                  title: Text(
                    DateFormat('M月d日 EEEE HH:mm', 'zh_TW').format(date),
                  ),
                  trailing: const Icon(Icons.schedule),
                  onTap: () async {
                    final d = await showDatePicker(
                      context: ctx,
                      firstDate: DateTime.now().subtract(
                        const Duration(days: 1),
                      ),
                      lastDate: DateTime.now().add(
                        const Duration(days: 365 * 5),
                      ),
                      initialDate: date,
                    );
                    if (d == null) return;
                    if (!ctx.mounted) return;
                    final t = await showTimePicker(
                      context: ctx,
                      initialTime: TimeOfDay.fromDateTime(date),
                    );
                    setD(
                      () => date = DateTime(
                        d.year,
                        d.month,
                        d.day,
                        t?.hour ?? date.hour,
                        t?.minute ?? date.minute,
                      ),
                    );
                  },
                ),
                DropdownButtonFormField<String>(
                  initialValue: recurrence,
                  decoration: const InputDecoration(labelText: '重複'),
                  items: recurrenceOptions
                      .map(
                        (option) => DropdownMenuItem(
                          value: option.$1,
                          child: Text(option.$2),
                        ),
                      )
                      .toList(),
                  onChanged: (v) => setD(() => recurrence = v ?? ''),
                ),
                SwitchListTile(
                  title: const Text('提前 30 分鐘通知'),
                  value: remind,
                  onChanged: (v) => setD(() => remind = v),
                ),
              ],
            ),
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(ctx, false),
              child: const Text('取消'),
            ),
            FilledButton(
              onPressed: () => Navigator.pop(ctx, true),
              child: const Text('儲存'),
            ),
          ],
        ),
      ),
    );
    if (result == true && title.text.trim().isNotEmpty) {
      if (old != null) {
        await ReminderService.cancel(old, _stableId(old));
        widget.store.events.remove(old);
      }
      final e = CalendarEntry(
        title: title.text.trim(),
        date: date,
        description: desc.text.trim(),
        remind: remind,
        rrule: recurrence,
      );
      widget.store.events.add(e);
      await widget.store.save();
      await ReminderService.scheduleAll(widget.store.events);
      if (widget.ble.connected) await widget.ble.sync(widget.store);
    }
  }
}

List<CalendarEntry> parseIcs(String raw) {
  final unfolded = raw.replaceAll(RegExp(r'\r?\n[ \t]'), '');
  final events = <CalendarEntry>[];
  Map<String, String>? current;
  for (final line in unfolded.split(RegExp(r'\r?\n'))) {
    if (line == 'BEGIN:VEVENT') {
      current = {};
      continue;
    }
    if (line == 'END:VEVENT' && current != null) {
      final dt = current['DTSTART'] ?? '';
      final timezoneId = current['DTSTART_TZID'];
      if (timezoneId != null && timezoneId != tz.local.name) {
        throw FormatException(
          '事件時區為 $timezoneId；目前僅能安全匯入手機時區 ${tz.local.name} 的行事曆。',
        );
      }
      final rrule = current['RRULE'] ?? '';
      _validateSupportedRRule(rrule);
      final match = RegExp(
        r'^(\d{4})(\d{2})(\d{2})(?:T(\d{2})(\d{2})(\d{2})?)?(Z?)$',
      ).firstMatch(dt);
      if (match == null) {
        throw const FormatException('VEVENT 缺少有效的 DTSTART，整份檔案未匯入。');
      } else {
        final parts = [
          int.parse(match[1]!),
          int.parse(match[2]!),
          int.parse(match[3]!),
          int.parse(match[4] ?? '8'),
          int.parse(match[5] ?? '0'),
          int.parse(match[6] ?? '0'),
        ];
        final validatedUtc = DateTime.utc(
          parts[0],
          parts[1],
          parts[2],
          parts[3],
          parts[4],
          parts[5],
        );
        if (validatedUtc.year != parts[0] ||
            validatedUtc.month != parts[1] ||
            validatedUtc.day != parts[2] ||
            validatedUtc.hour != parts[3] ||
            validatedUtc.minute != parts[4] ||
            validatedUtc.second != parts[5]) {
          throw const FormatException('DTSTART 日期或時間不存在，整份檔案未匯入。');
        }
        final utcTime = match[7] == 'Z';
        final date = utcTime
            ? DateTime.utc(
                parts[0],
                parts[1],
                parts[2],
                parts[3],
                parts[4],
                parts[5],
              ).toLocal()
            : DateTime(
                parts[0],
                parts[1],
                parts[2],
                parts[3],
                parts[4],
                parts[5],
              );
        if (!utcTime &&
            (date.year != parts[0] ||
                date.month != parts[1] ||
                date.day != parts[2] ||
                date.hour != parts[3] ||
                date.minute != parts[4] ||
                date.second != parts[5])) {
          throw const FormatException('DTSTART 不存在於手機時區，整份檔案未匯入。');
        }
        events.add(
          CalendarEntry(
            uid: current['UID'],
            title: _icsUnescape(current['SUMMARY'] ?? '行事曆事件'),
            date: date,
            description: _icsUnescape(current['DESCRIPTION'] ?? ''),
            rrule: rrule,
          ),
        );
      }
      current = null;
      continue;
    }
    if (current != null) {
      final colon = line.indexOf(':');
      if (colon > 0) {
        final property = line.substring(0, colon);
        final key = property.split(';').first.toUpperCase();
        current[key] = line.substring(colon + 1);
        if (key == 'DTSTART') {
          final tzid = RegExp(
            r'(?:^|;)TZID="?([^";:]+)"?',
            caseSensitive: false,
          ).firstMatch(property)?.group(1)?.trim();
          if (tzid != null) current['DTSTART_TZID'] = tzid;
        }
      }
    }
  }
  return events;
}

String buildIcs(List<CalendarEntry> events) {
  String esc(String s) => s
      .replaceAll('\\', '\\\\')
      .replaceAll('\n', '\\n')
      .replaceAll(',', '\\,')
      .replaceAll(';', '\\;');
  final b = StringBuffer('');
  void writeLine(String value) {
    var column = 0;
    for (final rune in value.runes) {
      final character = String.fromCharCode(rune);
      final byteLength = utf8.encode(character).length;
      if (column + byteLength > 75) {
        b.write('\r\n ');
        column = 1;
      }
      b.write(character);
      column += byteLength;
    }
    b.write('\r\n');
  }

  writeLine('BEGIN:VCALENDAR');
  writeLine('VERSION:2.0');
  writeLine('PRODID:-//SmartSchoolbag//ZH-TW');
  for (final e in events) {
    writeLine('BEGIN:VEVENT');
    writeLine('UID:${e.uid}');
    writeLine('DTSTART:${DateFormat("yyyyMMdd'T'HHmmss").format(e.date)}');
    writeLine('SUMMARY:${esc(e.title)}');
    writeLine('DESCRIPTION:${esc(e.description)}');
    if (e.rrule.isNotEmpty) writeLine('RRULE:${e.rrule}');
    writeLine('END:VEVENT');
  }
  writeLine('END:VCALENDAR');
  return b.toString();
}

String _icsUnescape(String s) => s
    .replaceAll(r'\n', '\n')
    .replaceAll(r'\,', ',')
    .replaceAll(r'\;', ';')
    .replaceAll(r'\\', r'\');
int _stableId(CalendarEntry e) {
  var h = 2166136261;
  for (final b in utf8.encode(e.uid)) {
    h = ((h ^ b) * 16777619) & 0x7fffffff;
  }
  return h;
}

void _validateSupportedRRule(String rule) {
  if (rule.trim().isEmpty) return;
  const supported = {'FREQ', 'INTERVAL', 'COUNT', 'UNTIL'};
  final parts = rule.split(';');
  final values = <String, String>{};
  for (final part in parts) {
    final index = part.indexOf('=');
    if (index <= 0) throw FormatException('無法解析重複規則：$rule');
    final key = part.substring(0, index).toUpperCase();
    if (!supported.contains(key)) {
      throw FormatException('目前不支援行事曆重複規則 $key；該檔案未匯入。');
    }
    if (values.containsKey(key)) throw FormatException('重複規則欄位重複：$key');
    values[key] = part.substring(index + 1);
  }
  final frequency = values['FREQ']?.toUpperCase();
  if (!{'DAILY', 'WEEKLY', 'MONTHLY'}.contains(frequency)) {
    throw FormatException('目前只支援每日、每週、每月重複事件。');
  }
  if ((int.tryParse(values['INTERVAL'] ?? '1') ?? 0) < 1 ||
      (values['COUNT'] != null && (int.tryParse(values['COUNT']!) ?? 0) < 1) ||
      (values['UNTIL'] != null &&
          !RegExp(r'^\d{8}(?:T\d{6}Z?)?$').hasMatch(values['UNTIL']!))) {
    throw FormatException('行事曆重複規則的間隔、次數或截止日期格式無效。');
  }
  if (values.containsKey('COUNT') && values.containsKey('UNTIL')) {
    throw const FormatException('重複規則不能同時設定 COUNT 與 UNTIL。');
  }
  final until = values['UNTIL'];
  if (until != null) {
    final match = RegExp(
      r'^(\d{4})(\d{2})(\d{2})(?:T(\d{2})(\d{2})(\d{2})Z?)?$',
    ).firstMatch(until);
    if (match != null) {
      final parts = [
        int.parse(match[1]!),
        int.parse(match[2]!),
        int.parse(match[3]!),
        int.parse(match[4] ?? '23'),
        int.parse(match[5] ?? '59'),
        int.parse(match[6] ?? '59'),
      ];
      final date = DateTime.utc(
        parts[0],
        parts[1],
        parts[2],
        parts[3],
        parts[4],
        parts[5],
      );
      if (date.year != parts[0] ||
          date.month != parts[1] ||
          date.day != parts[2] ||
          date.hour != parts[3] ||
          date.minute != parts[4] ||
          date.second != parts[5]) {
        throw const FormatException('重複規則的 UNTIL 日期無效。');
      }
    }
  }
}

class NotesPage extends StatefulWidget {
  const NotesPage({super.key, required this.store, required this.ble});
  final BagStore store;
  final BagBle ble;
  @override
  State<NotesPage> createState() => _NotesPageState();
}

class _NotesPageState extends State<NotesPage> {
  Future<void> _editNote(int? index) async {
    final original = index == null ? '' : widget.store.stickyNotes[index];
    final parts = original.split('\n');
    final titleController = TextEditingController(text: parts.first);
    final controller = TextEditingController(text: parts.skip(1).join('\n'));
    final value = await showDialog<String>(
      context: context,
      builder: (dialogContext) => AlertDialog(
        title: Text(index == null ? '新增便利貼' : '編輯便利貼'),
        content: SingleChildScrollView(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              TextField(
                controller: titleController,
                autofocus: true,
                maxLength: 16,
                decoration: const InputDecoration(labelText: '標題'),
              ),
              const SizedBox(height: 12),
              TextField(
                controller: controller,
                maxLines: 5,
                decoration: const InputDecoration(labelText: '內容'),
              ),
            ],
          ),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(dialogContext),
            child: const Text('取消'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(
              dialogContext,
              '${titleController.text.trim()}\n${controller.text.trim()}'
                  .trim(),
            ),
            child: const Text('儲存'),
          ),
        ],
      ),
    );
    controller.dispose();
    titleController.dispose();
    if (!mounted || value == null) return;
    if (value.isEmpty) {
      ScaffoldMessenger.of(
        context,
      ).showSnackBar(const SnackBar(content: Text('便利貼內容不能空白')));
      return;
    }
    if (utf8.encode(value).length > 120) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('電子紙每則便利貼最多 120 個 UTF-8 位元組')),
      );
      return;
    }
    if (index == null) {
      if (widget.store.stickyNotes.length >= 5) {
        ScaffoldMessenger.of(
          context,
        ).showSnackBar(const SnackBar(content: Text('電子紙最多顯示 5 則便利貼')));
        return;
      }
      widget.store.stickyNotes.add(value);
    } else {
      widget.store.stickyNotes[index] = value;
    }
    await _saveAndQueueSync();
  }

  Future<void> _saveAndQueueSync() async {
    await widget.store.save();
    await widget.ble.sync(widget.store);
    if (mounted) {
      setState(() {});
    }
  }

  @override
  Widget build(BuildContext context) => Column(
    children: [
      Expanded(
        child: ListView(
          padding: const EdgeInsets.all(16),
          children: [
            Text('便利貼 ${widget.store.stickyNotes.length} / 5'),
            const SizedBox(height: 4),
            Text(
              widget.ble.notesSyncStatus.startsWith('便利貼同步待處理')
                  ? '等待自動重試'
                  : widget.ble.notesSyncStatus,
            ),
            const SizedBox(height: 8),
            for (var i = 0; i < widget.store.stickyNotes.length; i++)
              Card(
                child: ListTile(
                  leading: const Icon(Icons.sticky_note_2_outlined),
                  title: Text(widget.store.stickyNotes[i].split('\n').first),
                  subtitle: widget.store.stickyNotes[i].contains('\n')
                      ? Text(
                          widget.store.stickyNotes[i]
                              .split('\n')
                              .skip(1)
                              .join(' '),
                          maxLines: 2,
                          overflow: TextOverflow.ellipsis,
                        )
                      : null,
                  onTap: () => _editNote(i),
                  trailing: IconButton(
                    tooltip: '刪除便利貼',
                    icon: const Icon(Icons.delete_outline),
                    onPressed: () async {
                      widget.store.stickyNotes.removeAt(i);
                      await _saveAndQueueSync();
                    },
                  ),
                ),
              ),
            if (widget.store.stickyNotes.isEmpty)
              const _Empty(text: '還沒有便利貼，新增後可在書包離線查看。'),
          ],
        ),
      ),
      Padding(
        padding: const EdgeInsets.fromLTRB(16, 0, 16, 16),
        child: SizedBox(
          width: double.infinity,
          child: FilledButton.icon(
            onPressed: () => _editNote(null),
            icon: const Icon(Icons.add),
            label: const Text('新增便利貼'),
          ),
        ),
      ),
    ],
  );
}

class ItemsPage extends StatelessWidget {
  const ItemsPage({super.key, required this.store, required this.ble});
  final BagStore store;
  final BagBle ble;
  @override
  Widget build(BuildContext context) => ListView(
    padding: const EdgeInsets.all(16),
    children: [
      for (final category in ['book', 'item']) ...[
        _SectionTitle(category == 'book' ? '課本' : '其他物品'),
        for (var i = 0; i < store.items.length; i++)
          if (store.items[i].category == category)
            ListTile(
              leading: Icon(
                category == 'book'
                    ? Icons.menu_book
                    : Icons.inventory_2_outlined,
              ),
              title: Text(store.items[i].name),
              subtitle: Text(
                store.items[i].tag.isEmpty
                    ? '尚未配對 RFID'
                    : '已配對 · ${store.items[i].tag}',
              ),
              onTap: () => _edit(context, store, ble, i),
              trailing: PopupMenuButton<String>(
                itemBuilder: (_) => const [
                  PopupMenuItem(value: 'pair', child: Text('掃描 RFID 配對')),
                  PopupMenuItem(value: 'delete', child: Text('刪除')),
                ],
                onSelected: (action) async {
                  if (action == 'pair') {
                    await ble.pairItem(i, store);
                    return;
                  }
                  final item = store.items.removeAt(i);
                  for (final lesson in store.lessons) {
                    lesson.itemNames.remove(item.name);
                  }
                  for (final names in store.dayItemOverrides.values) {
                    names.remove(item.name);
                  }
                  await store.save();
                },
              ),
            ),
      ],
      FilledButton.icon(
        onPressed: store.items.length >= 20
            ? null
            : () => _edit(context, store, ble, null),
        icon: const Icon(Icons.add),
        label: const Text('新增物品'),
      ),
    ],
  );
}

Future<void> chooseDayItems(
  BuildContext context,
  BagStore store,
  int weekday,
) async {
  final now = DateTime.now();
  final day = now.add(Duration(days: (weekday - now.weekday + 7) % 7));
  final selected = store.itemsFor(day).map((item) => item.name).toSet();
  bool pairedOnly = true;
  final result = await showDialog<String>(
    context: context,
    builder: (ctx) => StatefulBuilder(
      builder: (ctx, update) => AlertDialog(
        title: Text('星期${['一', '二', '三', '四', '五', '六', '日'][weekday - 1]}要帶'),
        content: SizedBox(
          width: double.maxFinite,
          child: SingleChildScrollView(
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                SwitchListTile(
                  contentPadding: EdgeInsets.zero,
                  title: const Text('只顯示已配對 RFID'),
                  value: pairedOnly,
                  onChanged: (value) => update(() => pairedOnly = value),
                ),
                for (final category in ['book', 'item']) ...[
                  Align(
                    alignment: Alignment.centerLeft,
                    child: Text(
                      category == 'book' ? '課本' : '其他物品',
                      style: const TextStyle(fontWeight: FontWeight.bold),
                    ),
                  ),
                  for (final item in store.items)
                    if (item.category == category &&
                        (!pairedOnly ||
                            item.tag.isNotEmpty ||
                            selected.contains(item.name)))
                      CheckboxListTile(
                        contentPadding: EdgeInsets.zero,
                        title: Text(item.name),
                        subtitle: Text(item.tag.isEmpty ? '未配對' : '已配對 RFID'),
                        value: selected.contains(item.name),
                        onChanged: (value) => update(() {
                          if (value == true) {
                            selected.add(item.name);
                          } else {
                            selected.remove(item.name);
                          }
                        }),
                      ),
                ],
              ],
            ),
          ),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx, 'auto'),
            child: const Text('依課表'),
          ),
          TextButton(
            onPressed: () => Navigator.pop(ctx),
            child: const Text('取消'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(ctx, 'save'),
            child: const Text('儲存'),
          ),
        ],
      ),
    ),
  );
  if (result == 'save') {
    store.dayItemOverrides[weekday] = selected.toList();
    await store.save();
  }
  if (result == 'auto') {
    store.dayItemOverrides.remove(weekday);
    await store.save();
  }
}

Future<void> _edit(
  BuildContext context,
  BagStore store,
  BagBle ble,
  int? index,
) async {
  final old = index == null ? null : store.items[index];
  final controller = TextEditingController(text: old?.name ?? '');
  var category = old?.category ?? 'item';
  final accepted = await showDialog<bool>(
    context: context,
    builder: (ctx) => StatefulBuilder(
      builder: (ctx, update) => AlertDialog(
        title: Text(old == null ? '新增物品' : '編輯物品'),
        content: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            TextField(
              controller: controller,
              autofocus: true,
              maxLength: 16,
              decoration: const InputDecoration(labelText: '名稱'),
            ),
            DropdownButtonFormField<String>(
              initialValue: category,
              decoration: const InputDecoration(labelText: '分類'),
              items: const [
                DropdownMenuItem(value: 'book', child: Text('課本')),
                DropdownMenuItem(value: 'item', child: Text('其他物品')),
              ],
              onChanged: (value) => update(() => category = value ?? 'item'),
            ),
          ],
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx, false),
            child: const Text('取消'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(ctx, true),
            child: const Text('儲存'),
          ),
        ],
      ),
    ),
  );
  final name = normalizeItemName(controller.text);
  controller.dispose();
  if (accepted != true || name.isEmpty || !context.mounted) return;
  if (store.items.any((item) => item != old && item.name == name)) {
    ScaffoldMessenger.of(
      context,
    ).showSnackBar(const SnackBar(content: Text('物品名稱不可重複')));
    return;
  }
  if (old == null) {
    store.items.add(BagItem(name, category: category));
  } else {
    final previous = old.name;
    old.name = name;
    old.category = category;
    for (final lesson in store.lessons) {
      for (var i = 0; i < lesson.itemNames.length; i++) {
        if (lesson.itemNames[i] == previous) lesson.itemNames[i] = name;
      }
    }
    for (final names in store.dayItemOverrides.values) {
      for (var i = 0; i < names.length; i++) {
        if (names[i] == previous) names[i] = name;
      }
    }
  }
  await store.save();
}

class GpsMapPage extends StatelessWidget {
  const GpsMapPage({super.key, required this.ble});
  final BagBle ble;

  Future<void> _openMap(BuildContext context) async {
    final latitude = ble.gpsLatitude;
    final longitude = ble.gpsLongitude;
    if (latitude == null || longitude == null) return;
    try {
      await const MethodChannel('smart_schoolbag/device').invokeMethod<void>(
        'openGpsMap',
        {'latitude': latitude, 'longitude': longitude},
      );
    } on PlatformException catch (error) {
      if (context.mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('無法開啟地圖：${error.message ?? '請安裝地圖 App'}')),
        );
      }
    }
  }

  @override
  Widget build(BuildContext context) => AnimatedBuilder(
    animation: ble,
    builder: (context, _) {
      final latitude = ble.gpsLatitude;
      final longitude = ble.gpsLongitude;
      return ListView(
        padding: const EdgeInsets.all(20),
        children: [
          Card(
            color: const Color(0xffe8f0eb),
            clipBehavior: Clip.antiAlias,
            child: SizedBox(
              height: 270,
              child: Stack(
                children: [
                  Positioned.fill(
                    child: CustomPaint(painter: _GpsMapSketchPainter()),
                  ),
                  Center(
                    child: Icon(
                      Icons.location_pin,
                      size: 54,
                      color: latitude == null
                          ? const Color(0xff78847f)
                          : const Color(0xff087b72),
                    ),
                  ),
                  Positioned(
                    left: 12,
                    top: 12,
                    child: Chip(
                      avatar: Icon(
                        latitude == null
                            ? Icons.gps_not_fixed
                            : Icons.gps_fixed,
                        size: 18,
                      ),
                      label: Text(latitude == null ? '等待書包 GPS 定位' : '書包位置'),
                      backgroundColor: Colors.white.withValues(alpha: .9),
                    ),
                  ),
                ],
              ),
            ),
          ),
          const SizedBox(height: 14),
          Card(
            child: ListTile(
              leading: const Icon(Icons.my_location_rounded),
              title: Text(
                latitude == null || longitude == null
                    ? ble.gps
                    : '${latitude.toStringAsFixed(6)}, ${longitude.toStringAsFixed(6)}',
              ),
              subtitle: Text(
                ble.gpsUpdatedAt == null
                    ? '尚未收到書包位置'
                    : '更新時間 ${DateFormat('M/d HH:mm:ss').format(ble.gpsUpdatedAt!)}',
              ),
            ),
          ),
          const SizedBox(height: 8),
          FilledButton.icon(
            onPressed: latitude == null || longitude == null
                ? null
                : () => _openMap(context),
            icon: const Icon(Icons.map_outlined),
            label: const Text('在手機地圖 App 查看位置'),
          ),
          const SizedBox(height: 8),
          const Text(
            '書包在室外取得 GPS 座標後，會經 Wi‑Fi 或 BLE 傳到手機。地圖底圖由手機地圖 App 載入；離線時仍可查看最近收到的座標。',
            style: TextStyle(color: Color(0xff687570), height: 1.45),
          ),
        ],
      );
    },
  );
}

class _GpsMapSketchPainter extends CustomPainter {
  @override
  void paint(Canvas canvas, Size size) {
    final road = Paint()
      ..color = Colors.white
      ..strokeWidth = 17
      ..style = PaintingStyle.stroke
      ..strokeCap = StrokeCap.round;
    final minorRoad = Paint()
      ..color = const Color(0xffc9d8cf)
      ..strokeWidth = 2
      ..style = PaintingStyle.stroke;
    for (var i = 1; i < 5; i++) {
      final y = size.height * i / 5;
      final x = size.width * i / 5;
      canvas.drawLine(Offset(0, y), Offset(size.width, y + 16), minorRoad);
      canvas.drawLine(Offset(x, 0), Offset(x + 20, size.height), minorRoad);
    }
    final route = Path()
      ..moveTo(size.width * .08, size.height * .82)
      ..cubicTo(
        size.width * .3,
        size.height * .62,
        size.width * .52,
        size.height * .72,
        size.width * .91,
        size.height * .2,
      );
    canvas.drawPath(route, road);
  }

  @override
  bool shouldRepaint(covariant _GpsMapSketchPainter oldDelegate) => false;
}

class DevicePage extends StatelessWidget {
  const DevicePage({super.key, required this.store, required this.ble});
  final BagStore store;
  final BagBle ble;
  @override
  Widget build(BuildContext context) => AnimatedBuilder(
    animation: Listenable.merge([store, ble]),
    builder: (context, _) => ListView(
      padding: const EdgeInsets.all(16),
      children: [
        Card(
          child: ListTile(
            leading: Icon(
              Icons.wifi,
              color: ble.connected ? Colors.teal : null,
            ),
            title: Text(ble.status),
            subtitle: const Text(
              '先在手機 Wi-Fi 設定連上 SmartSchoolbag（密碼由裝置擁有者設定）',
            ),
            trailing: IconButton(
              onPressed: () => ble.connectWifi(store),
              icon: const Icon(Icons.sync),
              tooltip: '連線並同步',
            ),
          ),
        ),
        OutlinedButton.icon(
          onPressed: ble.openWifiSettings,
          icon: const Icon(Icons.settings),
          label: const Text('開啟手機 Wi-Fi 設定'),
        ),
        if (ble.connected)
          FilledButton.icon(
            onPressed: () => ble.sync(store),
            icon: const Icon(Icons.sync),
            label: const Text('同步課表、行事、物品與便利貼'),
          ),
        const _SectionTitle('GPS 與安全範圍'),
        Card(
          child: Padding(
            padding: const EdgeInsets.fromLTRB(16, 12, 16, 16),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    const Icon(
                      Icons.gps_fixed_rounded,
                      color: Color(0xff087a79),
                    ),
                    const SizedBox(width: 10),
                    Expanded(
                      child: Text(
                        ble.gpsLatitude == null
                            ? ble.gps
                            : '已定位 · ${ble.gpsLatitude!.toStringAsFixed(6)}, ${ble.gpsLongitude!.toStringAsFixed(6)}',
                        style: const TextStyle(fontWeight: FontWeight.w700),
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 5),
                Text(
                  ble.gpsUpdatedAt == null
                      ? '連上書包 Wi-Fi 後讀取定位狀態'
                      : '最近更新 ${DateFormat('M/d HH:mm:ss').format(ble.gpsUpdatedAt!)}',
                  style: Theme.of(context).textTheme.bodySmall,
                ),
                const SizedBox(height: 12),
                OutlinedButton.icon(
                  onPressed: ble.gpsLatitude == null || ble.gpsLongitude == null
                      ? null
                      : () async {
                          await store.setSafeZoneCenter(
                            ble.gpsLatitude!,
                            ble.gpsLongitude!,
                          );
                          if (context.mounted) {
                            ScaffoldMessenger.of(context).showSnackBar(
                              const SnackBar(
                                content: Text('已將目前 GPS 位置設為安全範圍中心'),
                              ),
                            );
                          }
                        },
                  icon: const Icon(Icons.my_location_rounded),
                  label: const Text('以目前定位設定中心'),
                ),
                SwitchListTile.adaptive(
                  contentPadding: EdgeInsets.zero,
                  title: const Text('手機本機安全範圍通知'),
                  subtitle: Text(
                    store.safeZoneLatitude == null
                        ? '先取得定位並設定中心'
                        : '僅在 App 前景、Wi-Fi 連線且 GPS 有定位時檢查',
                  ),
                  value: store.safeZoneAlertsEnabled,
                  onChanged: (enabled) {
                    if (enabled &&
                        (store.safeZoneLatitude == null ||
                            store.safeZoneLongitude == null)) {
                      ScaffoldMessenger.of(context).showSnackBar(
                        const SnackBar(content: Text('請先取得 GPS 定位並設定安全範圍中心')),
                      );
                      return;
                    }
                    unawaited(store.setSafeZoneAlertsEnabled(enabled));
                  },
                ),
                Row(
                  children: [
                    const Text('半徑'),
                    Expanded(
                      child: Slider(
                        min: 50,
                        max: 5000,
                        divisions: 49,
                        label: '${store.safeZoneRadiusMeters.round()} 公尺',
                        value: store.safeZoneRadiusMeters
                            .clamp(50, 5000)
                            .toDouble(),
                        onChanged: store.previewSafeZoneRadius,
                        onChangeEnd: (value) =>
                            unawaited(store.setSafeZoneRadius(value)),
                      ),
                    ),
                    Text('${store.safeZoneRadiusMeters.round()} m'),
                  ],
                ),
                if (store.latestSafeZoneDistanceMeters != null)
                  Text(
                    '目前距中心約 ${store.latestSafeZoneDistanceMeters!.round()} 公尺',
                    style: Theme.of(context).textTheme.bodySmall,
                  ),
                const SizedBox(height: 4),
                const Text(
                  '通知由手機本機判斷；這不是雲端即時追蹤。GPS 未定位、Wi-Fi 中斷或 App 未在前景時不會即時告警。',
                  style: TextStyle(
                    fontSize: 12,
                    height: 1.4,
                    color: Color(0xff68756f),
                  ),
                ),
              ],
            ),
          ),
        ),
        Card(
          child: ListTile(
            leading: const Icon(Icons.info_outline),
            title: const Text('韌體版本'),
            subtitle: Text(ble.firmwareVersion),
          ),
        ),
        OutlinedButton.icon(
          onPressed: () => _exportBackup(store),
          icon: const Icon(Icons.download),
          label: const Text('匯出本機資料備份'),
        ),
        OutlinedButton.icon(
          onPressed: () => _importBackup(context, store, ble),
          icon: const Icon(Icons.upload_file),
          label: const Text('還原本機資料備份'),
        ),
        OutlinedButton.icon(
          onPressed: () => _clearLocalData(context, store),
          icon: const Icon(Icons.delete_outline),
          label: const Text('清除本機資料'),
        ),
        const Card(
          child: Padding(
            padding: EdgeInsets.all(16),
            child: Text(
              'RFID 直接對應手機物品清單中的名稱。首次綁定時，在電子紙「今日要帶」頁選取物品後刷卡；手機收到名稱與 UID 後會保存，之後同步回 ESP32。科目與物品名稱可自行新增，不限預設科目。',
            ),
          ),
        ),
      ],
    ),
  );
}

Future<void> _exportBackup(BagStore store) async {
  final payload = jsonEncode({
    'version': 1,
    'exportedAt': DateTime.now().toIso8601String(),
    'items': store.items.map((e) => e.toJson()).toList(),
    'lessons': store.lessons.map((e) => e.toJson()).toList(),
    'events': store.events.map((e) => e.toJson()).toList(),
    'stickyNote': store.stickyNote,
    'stickyNotes': store.stickyNotes,
    'dayItemOverrides': store.dayItemOverrides.map(
      (day, names) => MapEntry('$day', names),
    ),
    'locationSharing': store.locationSharing,
    'safeZoneAlertsEnabled': store.safeZoneAlertsEnabled,
    'safeZoneLatitude': store.safeZoneLatitude,
    'safeZoneLongitude': store.safeZoneLongitude,
    'safeZoneRadiusMeters': store.safeZoneRadiusMeters,
  });
  await FilePicker.platform.saveFile(
    fileName: '智慧電子書包備份.json',
    bytes: Uint8List.fromList(
      utf8.encode(
        const JsonEncoder.withIndent('  ').convert(jsonDecode(payload)),
      ),
    ),
    type: FileType.custom,
    allowedExtensions: ['json'],
  );
}

Future<void> _importBackup(
  BuildContext context,
  BagStore store,
  BagBle ble,
) async {
  final picked = await FilePicker.platform.pickFiles(
    type: FileType.custom,
    allowedExtensions: ['json'],
    withData: true,
  );
  final bytes = picked?.files.single.bytes;
  if (bytes == null) return;
  if (!context.mounted) return;
  try {
    final data = Map<String, dynamic>.from(
      jsonDecode(utf8.decode(bytes)) as Map,
    );
    if (data['version'] != 1) throw const FormatException('不支援的備份版本');
    final items = (data['items'] as List)
        .map((e) => BagItem.fromJson(Map<String, dynamic>.from(e)))
        .toList();
    final lessons = (data['lessons'] as List)
        .map((e) => Lesson.fromJson(Map<String, dynamic>.from(e)))
        .toList();
    final events = (data['events'] as List)
        .map((e) => CalendarEntry.fromJson(Map<String, dynamic>.from(e)))
        .toList();
    final stickyNotes =
        (data['stickyNotes'] as List<dynamic>? ??
                <dynamic>[data['stickyNote'] as String? ?? ''])
            .whereType<String>()
            .map((note) => note.trim())
            .where((note) => note.isNotEmpty)
            .toList();
    if (stickyNotes.length > 5 ||
        stickyNotes.any((note) => utf8.encode(note).length > 120)) {
      throw const FormatException('便利貼最多 5 則，每則最多 120 個 UTF-8 位元組');
    }
    final overrides = <int, List<String>>{};
    final rawOverrides = data['dayItemOverrides'] as Map? ?? {};
    for (final entry in rawOverrides.entries) {
      final day = int.tryParse(entry.key.toString());
      if (day == null || day < 1 || day > 7 || entry.value is! List) {
        throw const FormatException('星期清單格式錯誤');
      }
      overrides[day] = (entry.value as List)
          .whereType<String>()
          .where((name) => items.any((item) => item.name == name))
          .toList();
    }
    final eventIds = <String>{};
    for (final event in events) {
      if (!eventIds.add(event.uid)) event.uid = _newCalendarUid();
    }
    if (items.length > 20) throw const FormatException('備份物品超過裝置上限 20 項');
    if (items.any((item) => item.name.isEmpty)) {
      throw const FormatException('備份內有空白的物品名稱');
    }
    if (items.map((item) => item.name).toSet().length != items.length) {
      throw const FormatException('備份內有重複的物品名稱');
    }
    final tagUids = items
        .map((item) => item.tag)
        .where((uid) => uid.isNotEmpty)
        .toSet();
    if (items.any(
      (item) =>
          item.tag.isNotEmpty &&
          !RegExp(r'^(?:[0-9A-F]{2}){1,10}$').hasMatch(item.tag),
    )) {
      throw const FormatException('備份內有無效的 RFID UID。');
    }
    if (tagUids.length != items.where((item) => item.tag.isNotEmpty).length) {
      throw const FormatException('備份內有重複的 RFID UID，請先修正標籤配對。');
    }
    final importedLatitude = (data['safeZoneLatitude'] as num?)?.toDouble();
    final importedLongitude = (data['safeZoneLongitude'] as num?)?.toDouble();
    if ((importedLatitude == null) != (importedLongitude == null) ||
        (importedLatitude != null &&
            (importedLatitude < -90 || importedLatitude > 90)) ||
        (importedLongitude != null &&
            (importedLongitude < -180 || importedLongitude > 180))) {
      throw const FormatException('備份內的安全範圍座標無效');
    }
    final importedSafeZoneEnabled =
        (data['safeZoneAlertsEnabled'] as bool? ??
            (data['locationSharing'] as bool? ?? false)) &&
        importedLatitude != null;
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('還原本機資料？'),
        content: const Text('目前課表、物品與行事曆會由備份內容取代。'),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(ctx, false),
            child: const Text('取消'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(ctx, true),
            child: const Text('還原'),
          ),
        ],
      ),
    );
    if (confirmed != true) return;
    await ReminderService.cancelAllEvents(store.events);
    store.items
      ..clear()
      ..addAll(items);
    store.lessons
      ..clear()
      ..addAll(lessons);
    store.events
      ..clear()
      ..addAll(events);
    store.dayItemOverrides
      ..clear()
      ..addAll(overrides);
    store.stickyNotes
      ..clear()
      ..addAll(stickyNotes);
    store
      ..locationSharing = importedSafeZoneEnabled
      ..safeZoneAlertsEnabled = importedSafeZoneEnabled
      ..safeZoneLatitude = importedLatitude
      ..safeZoneLongitude = importedLongitude
      ..safeZoneRadiusMeters =
          ((data['safeZoneRadiusMeters'] as num?)?.toDouble() ?? 200)
              .clamp(50, 5000)
              .toDouble()
      ..outsideSafeZone = null
      ..latestSafeZoneDistanceMeters = null
      ..lastSync = null
      ..resetToday = true;
    for (final item in store.items) {
      item.checked = false;
    }
    await store.save();
    await ReminderService.scheduleAll(store.events);
    if (ble.connected) await ble.sync(store);
    if (context.mounted) {
      ScaffoldMessenger.of(
        context,
      ).showSnackBar(const SnackBar(content: Text('備份資料已還原至手機。')));
    }
  } catch (e) {
    if (context.mounted) {
      ScaffoldMessenger.of(
        context,
      ).showSnackBar(SnackBar(content: Text('無法還原備份：$e')));
    }
  }
}

Future<void> _clearLocalData(BuildContext context, BagStore store) async {
  final ok = await showDialog<bool>(
    context: context,
    builder: (ctx) => AlertDialog(
      title: const Text('清除本機資料？'),
      content: const Text('會刪除課表、物品、行事曆與提醒；ESP32 裡的 RFID 配對資料不會被遠端清除。'),
      actions: [
        TextButton(
          onPressed: () => Navigator.pop(ctx, false),
          child: const Text('取消'),
        ),
        FilledButton(
          onPressed: () => Navigator.pop(ctx, true),
          child: const Text('清除'),
        ),
      ],
    ),
  );
  if (ok != true) return;
  await ReminderService.cancelAllEvents(store.events);
  store.dayItemOverrides.clear();
  store.items.clear();
  store.lessons.clear();
  store.events.clear();
  store.locationSharing = false;
  store.safeZoneAlertsEnabled = false;
  store.safeZoneLatitude = null;
  store.safeZoneLongitude = null;
  store.safeZoneRadiusMeters = 200;
  store.outsideSafeZone = null;
  store.latestSafeZoneDistanceMeters = null;
  store.lastSync = null;
  store.lastRfid = '';
  store.stickyNotes.clear();
  await store.save();
}

class _SectionTitle extends StatelessWidget {
  const _SectionTitle(this.text);
  final String text;
  @override
  Widget build(BuildContext c) => Padding(
    padding: const EdgeInsets.fromLTRB(8, 18, 8, 6),
    child: Align(
      alignment: Alignment.centerLeft,
      child: Text(
        text,
        style: Theme.of(
          c,
        ).textTheme.titleMedium?.copyWith(fontWeight: FontWeight.bold),
      ),
    ),
  );
}

class _Empty extends StatelessWidget {
  const _Empty({required this.text});
  final String text;
  @override
  Widget build(BuildContext c) => Center(
    child: Padding(
      padding: const EdgeInsets.all(24),
      child: Text(text, textAlign: TextAlign.center),
    ),
  );
}
