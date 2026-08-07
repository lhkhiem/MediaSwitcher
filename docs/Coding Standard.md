# Coding Standard

## Language

C++20

## Naming

Class

PascalCase

```cpp
InputManager
```

Method

camelCase

```cpp
openSource()
```

Variable

camelCase

```cpp
currentFrame
```

Constant

UPPER_CASE

```cpp
MAX_INPUT
```

## Rules

Không dùng Global Variable

Không new/delete trực tiếp

Ưu tiên:

- std::unique_ptr
- std::shared_ptr

Không dùng Singleton tràn lan.

## Thread

UI Thread

Không Decode

Không Render

Worker Thread

Decode

GPU Thread

Render

## Logging

INFO

WARN

ERROR

DEBUG

Không dùng printf()

## Error

Không crash.

Luôn trả Error Code.

## Comment

Comment WHY.

Không comment WHAT.