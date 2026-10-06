# Markdown Glass

A simple markdown only viewer for Qt based desktop environments. Open a `.md`
file and read it the way GitHub would show it, with **no editor in the way**.
Press <kbd>Ctrl</kbd>+<kbd>F</kbd> to search or <kbd>F9</kbd> to hide the outline.

## Code you can copy

Hover a block for its copy button. Fences that name a language are highlighted:

```bash
# Install, then open any markdown file
sudo dnf install ./markdown-glass-*.rpm
markdown-glass README.md
```

A fence with no language stays plain, and inline code such as `git status`
gets its own small copy button.

```
plain text, exactly as written
```

## At a glance

| Feature | Shortcut | Notes |
|:--|:-:|:--|
| Outline sidebar | <kbd>F9</kbd> | Jump to any heading |
| Document preview | <kbd>Ctrl</kbd>+<kbd>M</kbd> | Replaces the scrollbar |
| Limit text width | <kbd>Ctrl</kbd>+<kbd>L</kbd> | Comfortable on wide monitors |
| Wrap code blocks | <kbd>Alt</kbd>+<kbd>Z</kbd> | Or scroll each block sideways |
| Edit in your editor | <kbd>Ctrl</kbd>+<kbd>E</kbd> | The view reloads when you save |

- [x] GitHub Flavored Markdown
- [x] Follows your desktop colour scheme
- [x] Printing and print preview
- [ ] Editing (on purpose)

> Remote images stay blocked until you allow them, so opening a file never
> phones home.

## Images

Local images load straight away, and animated GIFs play.

![A ball bouncing across a dark panel](images/bounce.gif)

<details>
<summary>What about raw HTML?</summary>

A common subset works: images with sizes, line breaks, keyboard keys,
sub<sub>script</sub> and super<sup>script</sup>, alignment, and collapsible
sections like this one.

</details>

## More formatting

1. Ordered lists
2. With nested items
   - like this one
   - and ~~struck through~~ text
3. And footnotes[^1]

```cpp
// Syntax highlighting comes from KDE's KSyntaxHighlighting
int main(int argc, char **argv)
{
    return argc > 1 ? 0 : 1;
}
```

[^1]: Footnotes collect at the end of the document.
