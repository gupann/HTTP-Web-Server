# Welcome to Our Documentation!

This page is designed to **test all features** of the `MarkdownHandler`.

---

## ✅ Basic Markdown Elements

- This is a bullet point
- **Bold**, _italic_, ~~strikethrough~~
- [Internal Link](another_page.md)
- [Nested Page](subdir/nested.md)
- Line break support  
  New line here.

---

## 🧮 Tables

| Feature     | Supported | Notes                       |
| ----------- | --------- | --------------------------- |
| Bold/Italic | ✅        | Standard Markdown styling   |
| Tables      | ✅        | Using GFM table extension   |
| Links       | ✅        | Internal `.md` links tested |

---

## 💡 Code Blocks

Inline `code` like this.

```cpp
#include <iostream>
int main() {
    std::cout << "Hello, MarkdownHandler!" << std::endl;
    return 0;
}
```

```python
def greet():
    print("Hello from Python!")
```
