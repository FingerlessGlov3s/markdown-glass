# Markdown Glass sample

A paragraph with **bold**, *italic*, ~~strike~~, `inline code`, a [link](https://example.com) and a footnote[^1].
Soft break continues the line. Press <kbd>Ctrl</kbd>+<kbd>C</kbd> to copy; H<sub>2</sub>O and x<sup>2</sup>.

## Lists

- First item
- Second item with `code`
  - Nested item
    - Deeper item
- [x] Done task
- [ ] Open task

1. One
2. Two
   1. Two point one
3. Three

## Code

No language, so no highlighting:

```
plain text block   with a very long line that should wrap when wrapping is enabled and scroll otherwise, lorem ipsum dolor sit amet consectetur
second line
```

Bash:

```bash
#!/usr/bin/env bash
set -euo pipefail
for f in *.md; do
    echo "Rendering $f"   # comment
done
```

```cpp
int main(int argc, char **argv) { return argc > 1 ? 0 : 1; }
```

## Quote and table

> A blockquote with **bold** text.
>
> Second paragraph of the quote.

| Name | Type | Description |
|:-----|:----:|------------:|
| `width` | int | Limits the reading width |
| `wrap` | bool | Wraps code blocks |
| `minimap` | bool | Shows the document preview on the scrollbar, with long text that wraps in the cell |

---

<details>
<summary>Click to expand</summary>

Hidden content.

</details>

<p align="center">Centred paragraph via HTML with <b>bold</b>.</p>

![Alt text for image](missing.png)

### Heading 3
#### Heading 4
##### Heading 5
###### Heading 6

[^1]: The footnote text.
