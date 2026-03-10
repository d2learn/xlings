# mcpp-style-ref 参考

来自 [mcpp-style-ref](https://github.com/mcpp-community/mcpp-style-ref) 的详细风格规则。

## 一、标识符命名

### 1.0 类型 — PascalCase（大驼峰）

```cpp
struct StyleRef {
    using FileNameType = std::string;
};
```

### 1.1 对象/数据成员 — camelCase（小驼峰）

```cpp
struct StyleRef {
    std::string fileName;
};
StyleRef mcppStyle;
```

### 1.2 函数 — snake_case（下划线）

```cpp
void load_config_file(const std::string& fileName);
void parse_();
int max_retry_count();
```

### 1.3 私有 — `_` 后缀

私有的数据成员和函数使用 `_` 后缀：

```cpp
private:
    std::string fileName_;
    void parse_(const std::string& config);
```

### 1.4 空格

运算符两侧加空格以增强可读性：`T x { ... }`、`int n { 42 }`。

### 1.5 其他

- 常量：`MAX_SIZE`、`DEFAULT_TIMEOUT`
- 全局：`gStyleRef`、`g_debug`
- 模板命名：遵循类/函数命名风格

---

## 二、模块化

### 模块文件结构

```cpp
module;              // 可选的全局模块片段
#include <xxx>       // 需要传统头文件时

export module module_name;
// export import :partition;
// import :partition;

import std;
import xxx;

export int add(int a, int b) {
    return a + b;
}
```

### .cppm 与 .h/.hpp

使用 `.cppm` 作为模块接口。用 `export` 关键字导出：

```cpp
export module mcpplibs;

export int add(int a, int b) {
    return a + b;
}
```

### 接口与实现

合并（全部在 .cppm）与分离（.cppm + .cpp）均有效。

**合并于 .cppm** — 见上方「.cppm 与 .h/.hpp」：导出与实现在同一文件。

**方式一：命名空间隔离**

```cpp
export module mcpplibs;

namespace mcpplibs_impl {
    int add(int a, int b) { return a + b; }
}

export namespace mcpplibs {
    using mcpplibs_impl::add;
};
```

**方式二：分离（.cppm + .cpp）**

- `.cppm`：仅接口 — `export module error;` + `export struct Error { void test(); };`
- `.cpp`：实现 — `module error;` + 函数体

简单模块用合并；需隐藏实现或减少编译依赖时用分离。

### 多文件模块

```
a/
├── a1.cppm     # module a:a1（内部分区）
├── a2.cppm     # export module a:a2
├── b/
│   ├── b1.cppm # export module a.b:b1
│   └── b2.cppm # export module a.b:b2
├── b.cppm      # export module a.b
└── c.cppm      # module a.c
a.cppm          # export module a
```

- **可导出分区**：`export module a:a2;` — 可被重新导出
- **内部分区**：`module a:a1;` — 不导出，仅模块内部使用

```cpp
// a.cppm
export module a;
export import :a2;
import :a1;
```

### 向前兼容

将传统 C/C++ 头文件封装到兼容模块中：

```cpp
module;

#include <lua.h>
// ...

export module lua;

export namespace lua {
    using lua_State = ::lua_State;
    // ...
}
```

### 其他

- 优先用 `constexpr` 替代宏
- 模板的静态成员：使用 `inline static`（C++17）确保单一定义

---

## 三、实践参考

### auto

用于迭代器、lambda、复杂类型。显式类型更清晰时避免使用。

### 花括号初始化

`int n { 42 }`、`std::vector<int> v { 1, 2, 3 }`、`Point p { 10, 20 }`。

### 智能指针

`std::make_unique`、`std::make_shared`；避免裸 `new`/`delete`。

### string_view

用于只读字符串参数。不拥有数据，调用方需保证底层数据有效。

### optional / expected

- `std::optional`：可有可无的值
- `std::expected`（C++23）：成功返回值或错误

### RAII

将资源与对象生命周期绑定。使用 `std::fstream`、`std::lock_guard` 等。
