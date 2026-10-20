import { defineConfig } from 'vitepress'

export default defineConfig({
  title: 'QtProbe',
  description: 'Guides and reference for inspecting EPICS Channel Access and pvAccess variables with QtProbe.',
  lang: 'en-US',
  base: process.env.DOCS_BASE || '/',
  outDir: './dist',
  srcExclude: ['public/**'],
  cleanUrls: false,
  head: [['meta', { name: 'theme-color', content: '#126384' }]],
  themeConfig: {
    siteTitle: 'QtProbe / docs',
    search: { provider: 'local' },
    outline: { level: [2, 3], label: 'On this page' },
    nav: [
      { text: 'Guide', link: '/get-started/first-run' },
      { text: 'Reference', link: '/reference/command-line' },
      { text: 'Development', link: '/develop/documentation' },
      { text: 'Repository', link: 'https://github.com/rtsoliday/probe' }
    ],
    sidebar: [
      { text: 'GET STARTED', items: [
        { text: 'Overview', link: '/' },
        { text: 'Install & build', link: '/get-started/install' },
        { text: 'Inspect your first PV', link: '/get-started/first-run' },
        { text: 'Launch QtProbe', link: '/get-started/launch' }
      ]},
      { text: 'OPERATE', collapsed: false, items: [
        { text: 'Workspace & appearance', link: '/operate/workspace' },
        { text: 'Live values & controls', link: '/operate/values' },
        { text: 'Adjust values', link: '/operate/adjustments' },
        { text: 'PV information', link: '/operate/information' },
        { text: 'Structure browser', link: '/operate/structures' },
        { text: 'Record explorer', link: '/operate/records' },
        { text: 'Arrays', link: '/operate/arrays' },
        { text: 'History & statistics', link: '/operate/history' },
        { text: 'Export & sessions', link: '/operate/files' },
        { text: 'Drag PVs between applications', link: '/operate/drag-and-drop' },
        { text: 'Help & manuals', link: '/operate/help' },
        { text: 'Troubleshooting', link: '/operate/troubleshooting' }
      ]},
      { text: 'REFERENCE', collapsed: true, items: [
        { text: 'Command-line options', link: '/reference/command-line' },
        { text: 'Keyboard shortcuts', link: '/reference/keyboard-shortcuts' },
        { text: 'Compatibility & current limits', link: '/understand/compatibility' }
      ]},
      { text: 'PROJECT & DEVELOPMENT', collapsed: true, items: [
        { text: 'Performance measurements', link: '/understand/performance' },
        { text: 'Tests', link: '/develop/testing' },
        { text: 'Maintain these docs', link: '/develop/documentation' },
        { text: 'Authors', link: '/project/authors' },
        { text: 'License', link: '/project/license' }
      ]}
    ]
  }
})
