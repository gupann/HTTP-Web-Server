# Feature Details

## 1. Directory Index
- Visit `/docs/` to see a dynamically generated list of all `.md` files.
- Files are cached for 5 seconds, so adding a new `.md` shows up almost immediately.
- Clicking a filename renders it as HTML.

## 2. Markdown-to-HTML Conversion
- Uses **cmark-gfm** to convert GitHub-flavored Markdown into safe HTML.
- Inline HTML is escaped by default; fenced code blocks remain intact.

### Example Code Block
```cpp
#include <iostream>

int main() {
    std::cout << "Hello, Markdown!" << std::endl;
    return 0;
}