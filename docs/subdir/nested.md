# Nested Paths & Raw Mode

## 1. Nested Directory Rendering
- This file lives at `/docs/subdir/nested.md` (a subdirectory).
- Links to sibling or parent files work automatically:
  - [Back to Home](../index.md)
  - [Another Page](../another_page.md)

When you navigate to `/docs/subdir/nested.md`, the handler:
1. Sanitizes `../` to prevent path traversal.
2. Converts this Markdown into HTML.
3. Wraps it in the same `md_wrapper.html` template.

## 2. Raw Mode
- To view the Markdown source instead of HTML, append `?raw=1`: