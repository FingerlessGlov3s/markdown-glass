# Changelog

Notable changes to Markdown Glass, newest first. Each release's section
becomes the RPM and Debian changelog entries, the release notes shown in
software centres (AppStream), and the GitHub release text, so write for
people using the application.

Format: a `## [version] - YYYY-MM-DD` heading, an optional one-line summary,
then `- ` bullet points (wrapped lines indented by two spaces). Work in
progress goes under `## [Unreleased]`, which is not published.

## [Unreleased]

Safer handling of untrusted documents.

- Link targets, file names and headings that look like HTML are shown as
  plain text in dialogs and tooltips, never as clickable links or images.
- SVG images that refer to anything outside themselves are refused more
  reliably, and very tall images are scaled down like very wide ones.
- Remote images, once allowed, are never fetched from this computer or the
  local network, even through a redirect.
- Mail links pass on only the address, subject and body, never attachments
  or extra recipients.
- Editor commands only ever receive the file name as a separate argument.
- Opening a second copy can no longer hand files to another user's program.
- A file swapped for a pipe or device while it is being opened is refused
  instead of hanging the viewer.
- Debian and Ubuntu packages are built with Debian's full set of hardening
  flags, including immediate binding, and the AppImage, including the Qt it
  bundles, with the same stack and relocation protections.
- A table with thousands of columns, a raw HTML block full of ampersands,
  a single word a million characters long, or an SVG whose shapes refer to
  each other in a chain can no longer make the viewer run out of memory or
  time.
- Remote images, once allowed, are fetched at most twenty at a time, from the
  address that was checked rather than looking the name up again.
- Without a per-user runtime directory the viewer no longer shares a socket
  in the system temporary directory; each launch opens its own window.
- Back and Forward keep their place when a file is declined at the "Large
  File" or "Not a Text File" question.
- Live reload notices a save that keeps the file's size and time.
- A link or symbolic link to the file already open no longer opens a second
  tab or adds a history entry.
- Animated images only visible in the scrollbar preview no longer keep
  playing, and headings with letters outside the Basic Multilingual Plane
  keep them in their anchors.
- Printing on paper too small for the footer, or to a printer reporting no
  resolution, no longer hangs.
- The markdown parser is updated to fix undefined behaviour when checking
  automatic links.

## [0.1.0] - 2026-10-05

First release.

- Shows markdown files the way GitHub does: tables, task lists, footnotes,
  strikethrough, autolinks, images and a common subset of raw HTML.
- Syntax highlighting for code blocks that name a language.
- Copy buttons on code blocks and inline code.
- Outline sidebar, document preview in place of the scrollbar, find, zoom
  and tabs.
- Optional reading width limit and code block wrapping.
- Remote images stay blocked until you allow them.
- Animated GIF and WebP images, with an option to stop them.
- Opens the file in your text editor, and reloads when it changes on disk.
- Printing and print preview.
- Packages for Fedora, AlmaLinux, openSUSE, Debian and Ubuntu, and an
  AppImage for everything else.
