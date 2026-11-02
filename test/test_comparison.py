from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tool"))
import compare_traces
import find_divergence


class ComparisonTests(unittest.TestCase):
    def test_digest_replay_forwarded_in_every_stage(self):
        args = SimpleNamespace(nes1="nes1", fceux="fceux", rom="game.nes",
                               work="unused", init_pc="", input="movie.in")
        for stage in ("stage1", "stage2", "stage3", "stage4"):
            with self.subTest(stage=stage), mock.patch.object(find_divergence, "run_emu") as run:
                find_divergence.run_both(args, stage, 20, ["--digest"])
                self.assertEqual(run.call_count, 2)
                self.assertTrue(all(call.args[-1] == "movie.in" for call in run.call_args_list))

    def test_trace_command_forwards_replay(self):
        with tempfile.TemporaryDirectory() as work, mock.patch.object(compare_traces.subprocess, "run") as run:
            run.return_value = SimpleNamespace(returncode=0)
            compare_traces.run_emu("nes1", "game.nes", str(Path(work) / "out"), 20, "1,20", "", 30, "movie.in")
            command = run.call_args.args[0]
            self.assertEqual(command[-2:], ["--input", "movie.in"])

    def test_empty_digests_are_not_identical(self):
        with mock.patch.object(find_divergence.cd, "iter_digest", return_value=iter(())):
            self.assertEqual(find_divergence.first_digest_difference("a", "b")[0], "empty digest")

    def test_empty_traces_are_errors(self):
        with tempfile.TemporaryDirectory() as work:
            path = Path(work) / "empty.txt"
            path.write_text("# header\n")
            with self.assertRaisesRegex(ValueError, "empty"):
                compare_traces.compare_file(path, path)

    def test_frame_selection_is_positive_and_deduplicated(self):
        self.assertEqual(compare_traces.frame_list("1-3,3,7"), [1, 2, 3, 7])
        for invalid in ("0", "-1", "3-1"):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                compare_traces.frame_list(invalid)


if __name__ == "__main__":
    unittest.main()
