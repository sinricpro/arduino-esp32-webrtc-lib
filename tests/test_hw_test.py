"""The hardware verdict must fail on radio starvation even when frames still arrive."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import hw_test


def diag(t, done, drop=0, q=16, codec=None):
    line = (f'DIAG {t}s: done {done} drop {drop} | avg frame 6122 B, avg send 50 ms, blocked 0 | '
            f'interval 200 ms q{q} | loop iters 30, main_loop avg 60000 us max 150000 us')
    return line + (f' | codec {codec}' if codec else '')


def heap(internal, streaming='yes', rssi=-66):
    return (f'Heap free {internal} (min 30000), internal {internal} (largest 28660), PSRAM free 3946020, '
            f'RSSI {rssi}, streaming: {streaming}')


def stream(seconds=40, frames_per_line=4, **kwargs):
    return [diag(100 + 2 * i, 0 if i == 0 else frames_per_line, **kwargs) for i in range(seconds // 2 + 1)]


class VerdictTests(unittest.TestCase):
    def judge(self, lines, **limits):
        metrics = hw_test.parse_log('\n'.join(lines))
        return metrics, hw_test.evaluate(metrics, hw_test.Limits(**limits))

    def test_healthy_stream_passes(self):
        lines = ['WebRTC offer for x with 4 ICE server URLs', 'WebRTC answer sent',
                 'E (1) UDP: sendto deferred (1 so far): errno 12 (Not enough space), 64 bytes',
                 heap(38000)] + stream() + [heap(37500)]
        metrics, (verdict, _) = self.judge(lines)
        self.assertEqual(verdict, 'PASS')
        self.assertEqual(metrics.fps, 2.0)
        self.assertEqual(metrics.codecs, ['jpeg'])

    def test_first_line_is_setup_not_streaming(self):
        metrics, _ = self.judge(stream(seconds=10))
        self.assertEqual(metrics.frames, 20)
        self.assertEqual(metrics.streaming_seconds, 10)

    def test_tx_exhaustion_fails_despite_frames(self):
        lines = stream() + ['E (9) UDP: sendto deferred (65 so far): errno 12 (Not enough space), 1097 bytes']
        _, (verdict, reasons) = self.judge(lines)
        self.assertEqual(verdict, 'FAIL')
        self.assertIn('TX buffers exhausted', reasons[0])

    def test_want_write_fails(self):
        _, (verdict, _) = self.judge(stream() + ['E (9) DTLS: mbedtls_ssl_read error: -26752'])
        self.assertEqual(verdict, 'FAIL')

    def test_dropped_frames_fail(self):
        _, (verdict, _) = self.judge(stream(drop=1))
        self.assertEqual(verdict, 'FAIL')

    def test_heap_decline_fails(self):
        _, (verdict, reasons) = self.judge([heap(38000)] + stream() + [heap(30000)])
        self.assertEqual(verdict, 'FAIL')
        self.assertIn('internal heap fell 8000 B', reasons[0])

    def test_idle_heap_lines_are_ignored_for_decline(self):
        _, (verdict, _) = self.judge([heap(60000, streaming='no')] + stream() + [heap(38000)])
        self.assertEqual(verdict, 'PASS')

    def test_no_diag_is_inconclusive(self):
        _, (verdict, reasons) = self.judge([heap(38000), 'Connected to SinricPro'])
        self.assertEqual(verdict, 'INCONCLUSIVE')
        self.assertTrue(any('SINRICPRO_WEBRTC_DIAG' in r for r in reasons))

    def test_short_stream_is_inconclusive(self):
        _, (verdict, _) = self.judge(stream(seconds=6))
        self.assertEqual(verdict, 'INCONCLUSIVE')

    def test_min_fps(self):
        _, (verdict, _) = self.judge(stream(frames_per_line=1), min_fps=1.0)
        self.assertEqual(verdict, 'FAIL')

    def test_weak_rssi_is_noted_not_failed(self):
        _, (verdict, reasons) = self.judge([heap(38000, rssi=-82)] + stream() + [heap(38000, rssi=-82)])
        self.assertEqual(verdict, 'PASS')
        self.assertTrue(any('RSSI reached -82' in r for r in reasons))

    def test_h264_codec_tag(self):
        metrics, _ = self.judge(stream(codec='h264'))
        self.assertEqual(metrics.codecs, ['h264'])

    def test_unanswered_offer_fails(self):
        _, (verdict, _) = self.judge(stream() + ['WebRTC answer failed: Camera timed out'])
        self.assertEqual(verdict, 'FAIL')


if __name__ == '__main__':
    unittest.main()
