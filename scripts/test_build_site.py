import io
import json
import unittest
from unittest.mock import patch

import build_site


def release(tag, published_at, prerelease=False, draft=False):
    return dict(tag_name=tag, published_at=published_at, prerelease=prerelease,
                draft=draft, html_url=build_site.RELEASE_URL + tag)


class ReleaseChannels(unittest.TestCase):
    def snapshot(self, stable, releases):
        def response(request, timeout):
            self.assertEqual(timeout, 30)
            data = stable if request.full_url.endswith('/latest') else releases
            return io.StringIO(json.dumps(data))
        with patch.object(build_site, 'urlopen', side_effect=response):
            return build_site.release_snapshot()

    def test_stable_promotion_updates_both_channels(self):
        stable = release('v1.7', '2026-10-08T13:00:00Z')
        beta = release('v1.7-beta.31', '2026-10-08T12:28:31Z', True)
        snapshot = self.snapshot(stable, [beta, stable])
        self.assertEqual(snapshot['stable']['tag'], 'v1.7')
        self.assertEqual(snapshot['beta']['tag'], 'v1.7')

    def test_newer_beta_retains_stable_channel(self):
        stable = release('v1.7', '2026-10-08T13:00:00Z')
        beta = release('v1.7.1-beta.1', '2026-10-08T14:00:00Z', True)
        snapshot = self.snapshot(stable, [stable, beta])
        self.assertEqual(snapshot['stable']['tag'], 'v1.7')
        self.assertEqual(snapshot['beta']['tag'], 'v1.7.1-beta.1')

    def test_withdrawn_beta_does_not_replace_stable(self):
        stable = release('v1.7', '2026-10-08T13:00:00Z')
        withdrawn = release('v1.7.1-beta.2', '2026-10-08T15:00:00Z', True, True)
        self.assertEqual(self.snapshot(stable, [withdrawn])['beta']['tag'], 'v1.7')

    def test_stable_only_repository(self):
        stable = release('v1.7', '2026-10-08T13:00:00Z')
        snapshot = self.snapshot(stable, [])
        self.assertEqual(snapshot['stable'], snapshot['beta'])


if __name__ == '__main__':
    unittest.main()
