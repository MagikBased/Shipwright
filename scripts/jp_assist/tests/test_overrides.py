import sys
import unittest
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from overrides import apply_context_override, apply_override  # noqa: E402


class OverrideTest(unittest.TestCase):
    def test_washi_pronoun_does_not_use_eagle_dictionary_sense(self):
        result = apply_override(
            "ワシ", "わし", {"meaning": "eagle", "senseId": "jmdict:eagle"}
        )
        self.assertEqual(result["meaning"], "I; me (typically used by an older man)")
        self.assertEqual(result["senseId"], "override:ワシ|わし")
        self.assertEqual(result["partOfSpeech"], "pronoun")
        self.assertIn("not 鷲", result["note"])

    def test_oira_is_singular_and_controller_label_is_not_vocabulary(self):
        oira = apply_override("オイラ", "おいら", {"meaning": "we; us"})
        self.assertEqual(oira["meaning"], "I; me (casual, rustic)")
        self.assertEqual(oira["partOfSpeech"], "pronoun")
        control = apply_override("c", "c", {"meaning": "letter C"})
        self.assertEqual(control["senseId"], "interface:c-button")
        self.assertEqual(control["partOfSpeech"], "interface label")

    def test_proper_names_are_identified_and_contextual_senses_win(self):
        proper = apply_override("サリア", "さりあ", {"meaning": "unknown"})
        self.assertTrue(proper["senseId"].startswith("proper:"))
        slingshot = apply_override("パチンコ", "ぱちんこ", {"meaning": "pachinko"})
        self.assertEqual(slingshot["meaning"], "slingshot")
        good = apply_override("よい", "よい", {"meaning": "evening"})
        self.assertEqual(good["meaning"], "good; fine; all right")
        buy = apply_override("かう", "かう", {"meaning": "raise an animal"})
        self.assertEqual(buy["partOfSpeech"], "verb")
        fruit = apply_override("実", "じつ", {"meaning": "truth"})
        self.assertEqual(fruit["dictionaryReading"], "み")
        self.assertEqual(fruit["senseId"], "override:実|み")

    def test_interface_and_inflection_overrides_correct_homographs(self):
        button = apply_override("ボタン", "ぼたん", {"meaning": "tree peony"})
        buy = apply_override("買える", "かえる", {"meaning": "to return home"})
        middle = apply_override("中", "なか", {"meaning": "medium"})
        defeated = apply_override("倒す", "たおす", {"meaning": "to recline a seat"})
        self.assertEqual(button["meaning"], "button; control button")
        self.assertEqual(buy["meaning"], "can buy; to be able to purchase")
        self.assertEqual(middle["partOfSpeech"], "suffix")
        self.assertEqual(middle["dictionaryReading"], "ちゅう")
        self.assertEqual(defeated["meaning"], "to defeat; to knock down; to bring down")

    def test_contextual_tutorial_and_dialogue_senses_are_selected(self):
        thing = apply_override("もの", "もの", {"meaning": "person"})
        yield_word = apply_override("まいる", "まいる", {"meaning": "to come"})
        mask = apply_override("面", "めん", {"meaning": "face"})
        target = apply_override("相手", "あいて", {"meaning": "companion"})
        viewpoint = apply_override("視点", "してん", {"meaning": "opinion"})
        self.assertEqual(thing["meaning"], "thing; that which; nominalizer")
        self.assertIn("give up", yield_word["meaning"])
        self.assertEqual(mask["meaning"], "mask")
        self.assertIn("target", target["meaning"])
        self.assertIn("camera", viewpoint["meaning"])

    def test_contractions_and_action_senses_replace_unrelated_homographs(self):
        self.assertIn("home", apply_override("ち", "ち", {"meaning": "blood"})["meaning"])
        self.assertIn("contraction", apply_override("ちゃ", "ちゃ", {"meaning": "tea"})["note"])
        self.assertIn("equip", apply_override("つける", "つける", {"meaning": "install"})["meaning"])
        self.assertIn("stance", apply_override("かまえる", "かまえる", {"meaning": "build"})["meaning"])
        self.assertEqual(apply_override("おうち", "おうち", {"meaning": "hollow"})["meaning"], "home; house")
        self.assertIn("ammunition", apply_override("タマ", "たま", {"meaning": "ball"})["meaning"])
        self.assertIn("try doing", apply_override("ごらん", "ごらん", {"meaning": "seeing"})["meaning"])
        self.assertIn("surprise", apply_override("え", "え", {"meaning": "perilla"})["meaning"])
        self.assertEqual(apply_override("ヘン", "へん", {"meaning": "radical"})["meaning"], "strange; odd; unusual")
        holy = apply_override("聖", "きよし", {"meaning": "personal name"})
        self.assertEqual(holy["dictionaryReading"], "せい")
        self.assertEqual(holy["senseId"], "override:聖|せい")
        self.assertIn("anyway", apply_override("ま", "ま", {"meaning": "just"})["meaning"])

    def test_castle_and_ranch_dialogue_homographs_use_contextual_senses(self):
        self.assertEqual(apply_override("マロン", "まろん", {"meaning": "chestnut"})["meaning"], "Malon")
        self.assertIn("Cucco", apply_override("コッコ", "こっこ", {"meaning": "treasury"})["meaning"])
        self.assertIn("to go", apply_override("いく", "いく", {"meaning": "awe"})["meaning"])
        self.assertIn("emphasis", apply_override("よ〜", "よ", {"meaning": "other"})["meaning"])
        self.assertIn("announcement", apply_override("告げ", "つげ", {"meaning": "boxwood"})["meaning"])
        self.assertIn("mask shop", apply_override("面屋", "おもや", {"meaning": "main building"})["meaning"])
        self.assertEqual(apply_override("ワン", "わん", {"meaning": "WAN"})["meaning"], "woof; bark")
        self.assertIn("everything", apply_override("ぜんぶ", "ぜんぶ", {"meaning": "front"})["meaning"])
        self.assertEqual(apply_override("カンバン", "かんばん", {"meaning": "kanban"})["meaning"], "signboard; sign")
        self.assertIn("appearance", apply_override("カッコ", "かっこ", {"meaning": "brackets"})["meaning"])
        self.assertEqual(apply_override("ザマス", "ざます", {"meaning": "noun"})["partOfSpeech"], "auxiliary verb")

    def test_ki_wo_tsukeru_is_split_from_attach_and_equip(self):
        equipped = apply_override("つける", "つける", {"meaning": "install"})
        careful = apply_context_override("つける", "つける", "気をつける", 2, equipped)
        self.assertEqual(careful["meaning"], "to be careful; to take care")
        self.assertEqual(careful["senseId"], "override:気をつける|きをつける")
        self.assertEqual(
            apply_context_override("つける", "つける", "盾をつける", 2, equipped),
            equipped,
        )


if __name__ == "__main__":
    unittest.main()
