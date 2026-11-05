import 'dart:convert';

import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:smart_schoolbag_app/main.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('legacy names migrate to categories without losing RFID', () {
    final book = BagItem.fromJson({'name': '國文課本', 'tag': 'AA BB CC DD'});
    final bottle = BagItem.fromJson({'name': '水壺'});
    final custom = BagItem.fromJson({'name': '生物學', 'category': 'book'});
    expect(book.isBook, isTrue);
    expect(book.tag, 'AA BB CC DD');
    expect(bottle.isBook, isFalse);
    expect(BagItem.fromJson(custom.toJson()).isBook, isTrue);
  });

  test(
    'explicit empty weekday stays empty instead of falling back to lessons',
    () {
      final store = BagStore();
      store.items.add(BagItem('國文課本', category: 'book'));
      store.lessons.add(
        Lesson(weekday: 1, period: 1, subject: '國文', itemNames: ['國文課本']),
      );
      final monday = DateTime(2026, 11, 9);
      expect(store.itemsFor(monday).map((item) => item.name), ['國文課本']);
      store.dayItemOverrides[1] = [];
      expect(store.itemsFor(monday), isEmpty);
      store.dayItemOverrides.remove(1);
      expect(store.itemsFor(monday).map((item) => item.name), ['國文課本']);
      store.dispose();
    },
  );

  test(
    'weekday selection uses the shared database and does not leak across days',
    () {
      final store = BagStore();
      final book = BagItem('自然課本', category: 'book');
      final bottle = BagItem('水壺', tag: '01 02 03 04');
      store.items.addAll([book, bottle]);
      store.dayItemOverrides[1] = [book.name];
      store.dayItemOverrides[7] = [bottle.name];
      expect(store.itemsFor(DateTime(2026, 11, 9)).single, same(book));
      expect(store.itemsFor(DateTime(2026, 11, 8)).single, same(bottle));
      bottle.setCheckedOn(7, true);
      expect(bottle.checkedOn(1), isFalse);
      expect(bottle.checkedOn(7), isTrue);
      store.dispose();
    },
  );

  test(
    'offline save and reload preserves categories, RFID, empty days and note fields',
    () async {
      SharedPreferences.setMockInitialValues({'seeded': true});
      final store = BagStore();
      store.items.add(BagItem('自訂教材', category: 'book', tag: '01 02 03 04'));
      store.dayItemOverrides[1] = ['自訂教材'];
      store.dayItemOverrides[7] = [];
      store.stickyNotes.add('明天要帶\n彩色筆與材料');
      await store.save();
      final restored = BagStore();
      await restored.load();
      expect(restored.items.single.category, 'book');
      expect(restored.items.single.tag, '01 02 03 04');
      expect(restored.dayItemOverrides[1], ['自訂教材']);
      expect(restored.dayItemOverrides.containsKey(7), isTrue);
      expect(restored.dayItemOverrides[7], isEmpty);
      expect(restored.stickyNotes, ['明天要帶\n彩色筆與材料']);
      final preferences = await SharedPreferences.getInstance();
      expect(
        jsonDecode(preferences.getString('dayItemOverrides')!)['7'],
        isEmpty,
      );
      store.dispose();
      restored.dispose();
    },
  );
}
