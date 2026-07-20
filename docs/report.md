# 🌸 Code Quality Analysis Report 🌸

## 📑 Table of Contents

- [Issue Score](#overall-score)
- [Metrics Details](#metrics-details)
- [Problem Files Ranking](#problem-files)
- [Diagnosis](#conclusion)

![Score](https://img.shields.io/badge/Score-80%25-green)

## Issue Score {#overall-score}

| Metrics Summary | Score |
|------|-------|
| **Issue Score** | **79.72/100** |
| Quality Level | 😐 Slightly stinky youth |

> A hint of fragrance, sometimes a whiff of funk—still safe to touch

### 📊 Statistics

| Metric | Value |
|--------|-------|
| Total Files | 65 |
| Skipped | 23 |
| Time | 224ms |

## Metrics Details {#metrics-details}

| Metrics Summary | Score | Status |
|:-----|------:|:------:|
| Cyclomatic Complexity | 14.57% | ✓✓ |
| Cognitive Complexity | 15.36% | ✓✓ |
| Nesting Depth | 11.34% | ✓✓ |
| Function Length | 2.96% | ✓✓ |
| File Length | 0.78% | ✓✓ |
| Parameter Count | 5.66% | ✓✓ |
| Code Duplication | 1.61% | ✓✓ |
| Structure Analysis | 3.38% | ✓✓ |
| Error Handling | 0.00% | ✓✓ |
| Comment Ratio | 89.46% | !! |
| Naming Convention | 29.34% | ✓ |

## Problem Files Ranking {#problem-files}

### 1. src\syntax.c

**Issue Score: 42.24**

**Issues**: 🔄 Complexity Issues: 6, ⚠️ Other Issues: 3, 🏗️ Structure Issues: 10, 📝 Comment Issues: 1, 🏷️ Naming Issues: 10

- 🔄 `loadLanguageSetting()` L213: Complexity: 22
- 🔄 `findLanguageFile()` L424: Complexity: 11
- 🔄 `loadLanguageSetting()` L213: Cognitive Complexity: 40
- 🔄 `findLanguageFile()` L424: Cognitive Complexity: 15
- 🔄 `editorSelectLanguageForFile()` L465: Cognitive Complexity: 15
- 🔍 ...and 23 more issues too smelly to list

### 2. src\output.c

**Issue Score: 37.12**

**Issues**: 🔄 Complexity Issues: 9, ⚠️ Other Issues: 3, 🏗️ Structure Issues: 2, 📝 Comment Issues: 1

- 🔄 `welcomeLineAt()` L195: Complexity: 11
- 🔄 `selectionBounds()` L245: Complexity: 11
- 🔄 `drawHorizontalScrollbar()` L267: Complexity: 11
- 🔄 `editorDrawRows()` L294: Complexity: 53
- 🔄 `welcomeLineAt()` L195: Cognitive Complexity: 15
- 🔍 ...and 7 more issues too smelly to list

### 3. src\keypress.c

**Issue Score: 33.05**

**Issues**: 🔄 Complexity Issues: 4, ⚠️ Other Issues: 1, 🏗️ Structure Issues: 2, 📝 Comment Issues: 1, 🏷️ Naming Issues: 1

- 🔄 `editorProcessKeypress()` L137: Complexity: 62
- 🔄 `editorApplyAtomicPaste()` L17: Cognitive Complexity: 13
- 🔄 `editorProcessKeypress()` L137: Cognitive Complexity: 70
- 🔄 `editorProcessKeypress()` L137: Nesting Depth: 4
- 📏 `editorProcessKeypress()` L137: 168 Size
- 🔍 ...and 3 more issues too smelly to list

### 4. src\help.c

**Issue Score: 32.09**

**Issues**: 🔄 Complexity Issues: 5, ⚠️ Other Issues: 3, 🏗️ Structure Issues: 2, 📝 Comment Issues: 1, 🏷️ Naming Issues: 3

- 🔄 `helpBuildBuffer()` L180: Complexity: 22
- 🔄 `editorHelp()` L323: Complexity: 16
- 🔄 `helpBuildBuffer()` L180: Cognitive Complexity: 28
- 🔄 `editorHelp()` L323: Cognitive Complexity: 28
- 🔄 `editorHelp()` L323: Nesting Depth: 6
- 🔍 ...and 6 more issues too smelly to list

### 5. src\mouse.c

**Issue Score: 29.78**

**Issues**: 🔄 Complexity Issues: 8, ⚠️ Other Issues: 3, 🏗️ Structure Issues: 2, 📝 Comment Issues: 1

- 🔄 `editorHandleModalViewInput()` L137: Complexity: 18
- 🔄 `editorUpdateDragPosition()` L178: Complexity: 17
- 🔄 `editorProcessMouseEvent()` L302: Complexity: 12
- 🔄 `editorHandleModalViewInput()` L137: Cognitive Complexity: 22
- 🔄 `editorUpdateDragPosition()` L178: Cognitive Complexity: 21
- 🔍 ...and 7 more issues too smelly to list

### 6. src\prompt.c

**Issue Score: 28.77**

**Issues**: 🔄 Complexity Issues: 5, ⚠️ Other Issues: 2, 🏗️ Structure Issues: 2, 📝 Comment Issues: 1, 🏷️ Naming Issues: 10

- 🔄 `editorPromptImpl(char *prompt, void (*callback)(char *, int),
                              int cancel_on_outside_click)()` L129: Complexity: 21
- 🔄 `promptEdit()` L105: Cognitive Complexity: 17
- 🔄 `editorPromptImpl(char *prompt, void (*callback)(char *, int),
                              int cancel_on_outside_click)()` L129: Cognitive Complexity: 29
- 🔄 `promptEdit()` L105: Nesting Depth: 5
- 🔄 `editorPromptImpl(char *prompt, void (*callback)(char *, int),
                              int cancel_on_outside_click)()` L129: Nesting Depth: 4
- 🔍 ...and 13 more issues too smelly to list

### 7. src\localization.c

**Issue Score: 26.86**

**Issues**: 🔄 Complexity Issues: 7, 🏗️ Structure Issues: 3, 📝 Comment Issues: 1, 🏷️ Naming Issues: 5

- 🔄 `readConfiguredLanguage()` L62: Complexity: 12
- 🔄 `loadLanguageFile()` L115: Complexity: 18
- 🔄 `readConfiguredLanguage()` L62: Cognitive Complexity: 16
- 🔄 `decodeValue()` L96: Cognitive Complexity: 18
- 🔄 `loadLanguageFile()` L115: Cognitive Complexity: 26
- 🔍 ...and 10 more issues too smelly to list

### 8. src\row.c

**Issue Score: 24.43**

**Issues**: 🔄 Complexity Issues: 4, ⚠️ Other Issues: 2, 🏗️ Structure Issues: 2, 📝 Comment Issues: 1

- 🔄 `editorUpdateSyntax()` L67: Complexity: 32
- 🔄 `editorUpdateSyntax()` L67: Cognitive Complexity: 40
- 🔄 `editorUpdateRow()` L258: Cognitive Complexity: 13
- 🔄 `editorUpdateSyntax()` L67: Nesting Depth: 4
- 📏 `editorUpdateSyntax()` L67: 189 Size
- 🔍 ...and 2 more issues too smelly to list

### 9. src\cli.c

**Issue Score: 24.04**

**Issues**: 🔄 Complexity Issues: 8, ⚠️ Other Issues: 2, 🏗️ Structure Issues: 2, 📝 Comment Issues: 1, 🏷️ Naming Issues: 6

- 🔄 `cliParse()` L39: Complexity: 14
- 🔄 `chooseName(char **names, int count, const char *prompt)()` L225: Complexity: 14
- 🔄 `cliResolveConfigFile(const char *configured_name)()` L308: Complexity: 14
- 🔄 `cliParse()` L39: Cognitive Complexity: 20
- 🔄 `*listIniNames(const char *relative_pattern,
                           const char *label, int *count)()` L186: Cognitive Complexity: 13
- 🔍 ...and 11 more issues too smelly to list

### 10. src\keybindings.c

**Issue Score: 23.65**

**Issues**: 🔄 Complexity Issues: 7, ⚠️ Other Issues: 1, 🏗️ Structure Issues: 3, 📝 Comment Issues: 1, 🏷️ Naming Issues: 7

- 🔄 `virtualKeyFromName()` L94: Complexity: 20
- 🔄 `parseBinding()` L125: Complexity: 13
- 🔄 `editorLoadKeyBindings()` L182: Complexity: 17
- 🔄 `virtualKeyFromName()` L94: Cognitive Complexity: 24
- 🔄 `parseBinding()` L125: Cognitive Complexity: 21
- 🔍 ...and 12 more issues too smelly to list

## Diagnosis {#conclusion}

🌸 **Slightly stinky youth** - A faint whiff, open a window and hope for the best

👍 Keep going, you're the clean freak of the coding world, a true code hygiene champion

---

*Generated by [fuck-u-code](https://github.com/Done-0/fuck-u-code)*