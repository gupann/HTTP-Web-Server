# Docs Demo: Markdown Rendering Feature

Welcome to the live demo of our web server’s new `/docs` feature! This documentation site not only explains itself but also *demonstrates* how Markdown files are automatically turned into styled HTML pages.

## Team Members
- Anmol Gupta
- Shlok Jhawar
- William Smith
- David Han

## Description of Project/Feature
Our goal is to make the server self-documenting by serving any `.md` file under `/docs` as styled HTML. Key capabilities include:
- **Automatic Rendering**: On-the-fly Markdown→HTML conversion using cmark-gfm.
- **Syntax Highlighting**: Code blocks get highlighted via Highlight.js.
- **Light/Dark Mode**: Styles adapt to the user’s system theme.
- **Directory Index**: Visiting `/docs/` shows a live list of all Markdown files.
- **Raw Mode**: Append `?raw=1` to view the original Markdown source.
- **HTTP Caching**: ETag and Last-Modified headers support conditional requests for efficiency.

Ready? Click below to see more details about each feature:

[Feature Details](anotherpage.md)