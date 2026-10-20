#!/usr/bin/env python3
"""Build and validate the QtProbe static documentation website."""

import argparse
from html.parser import HTMLParser
import os
from pathlib import Path
import re
import runpy
import shutil
import subprocess
import sys
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
SITE = ROOT / 'docs/site'
OUTPUT = SITE / 'dist'


class Page(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.ids = set()
        self.links = []
        self.title = False

    def handle_starttag(self, tag, attrs):
        data = dict(attrs)
        if 'id' in data:
            self.ids.add(data['id'])
        if tag == 'title':
            self.title = True
        for attr in ('href', 'src'):
            if data.get(attr):
                self.links.append(data[attr])


def verify(base):
    pages = {}
    for path in OUTPUT.rglob('*.html'):
        parser = Page()
        parser.feed(path.read_text(encoding='utf-8'))
        pages[path.resolve()] = parser
    if not pages:
        raise RuntimeError('No generated HTML pages found')
    issues, count = [], 0
    for path, page in pages.items():
        if not page.title:
            issues.append(f'{path.relative_to(OUTPUT)}: missing title')
        for url in page.links:
            split = urlsplit(url)
            if split.scheme or split.netloc:
                continue
            name = unquote(split.path)
            if name.startswith('/'):
                if not name.startswith(base):
                    issues.append(f'{path.relative_to(OUTPUT)}: URL escapes base {base}: {url}')
                    continue
                destination = (OUTPUT / name[len(base):]).resolve()
            else:
                destination = (path.parent / name).resolve() if name else path
            if not destination.is_relative_to(OUTPUT.resolve()):
                issues.append(f'{path.relative_to(OUTPUT)}: URL escapes site: {url}')
                continue
            if destination.is_dir():
                destination /= 'index.html'
            if not destination.exists() and not destination.suffix:
                destination = destination.with_suffix('.html')
            count += 1
            if not destination.exists():
                issues.append(f'{path.relative_to(OUTPUT)}: missing {url}')
            elif (split.fragment and destination in pages
                  and unquote(split.fragment) not in pages[destination].ids):
                issues.append(f'{path.relative_to(OUTPUT)}: missing anchor {url}')
    source = runpy.run_path(str(ROOT / 'scripts/sync-docs.py'))
    sections = source['guide_sections']()
    for title, _ in sections:
        route = source['SECTIONS'][title]
        path = (OUTPUT / (route.lstrip('/') + '.html')).resolve()
        if path not in pages or source['anchor'](title) not in pages[path].ids:
            issues.append(f'Missing guide section or anchor: {title}')
    if issues:
        raise RuntimeError('\n'.join(sorted(set(issues))))
    print(f'Validated {len(pages)} HTML pages, {count} local links/assets '
          f'and all {len(sections)} operator-guide sections.')


def main():
    # Documentation and npm build products are shared with other users.
    os.umask(0o022)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', default=os.environ.get('DOCS_BASE', '/'),
                        help='Hosting URL prefix, e.g. /manuals/QtProbe/ (default: /)')
    parser.add_argument('--check-only', action='store_true', help='Validate an existing build')
    args = parser.parse_args()
    if (not re.fullmatch(r'/(?:[A-Za-z0-9_.-]+/)*', args.base)
            or any(part in ('.', '..') for part in args.base.split('/'))):
        parser.error('--base must have leading/trailing slashes and no dot segments')
    if not args.check_only:
        subprocess.run([sys.executable, str(ROOT / 'scripts/sync-docs.py')], check=True)
        npm = shutil.which('npm') or shutil.which('npm.cmd')
        node = shutil.which('node')
        if not npm or not node:
            raise RuntimeError('Node.js 22 or newer and npm are required to build the website')
        version = subprocess.check_output([node, '--version'], text=True).strip()
        if int(version.lstrip('v').split('.')[0]) < 22:
            raise RuntimeError(f'Node.js 22 or newer is required; found {version}')
        npm_env = {**os.environ, 'npm_config_update_notifier': 'false'}
        if not (SITE / 'node_modules/vitepress').is_dir():
            subprocess.run([npm, 'ci', '--no-audit', '--no-fund'], cwd=SITE,
                           check=True, env=npm_env)
        subprocess.run([npm, 'run', 'build'], cwd=SITE, check=True,
                       env={**npm_env, 'DOCS_BASE': args.base})
    if not (OUTPUT / 'index.html').is_file():
        raise RuntimeError('No built website found; run make docs first')
    verify(args.base)
    if not args.check_only:
        destination = ROOT / 'docs/html'
        if destination.exists():
            shutil.rmtree(destination)
        shutil.copytree(OUTPUT, destination)
        # Static web servers must be able to read the copied site even when
        # the build shell uses a private umask.
        for path in [destination, *destination.rglob('*')]:
            path.chmod(0o755 if path.is_dir() else 0o644)
        print(f'Built {destination / "index.html"} for {args.base}')


if __name__ == '__main__':
    main()
