# Building

## CMake

```sh
# Configure
cmake --preset debug

# Build
cmake --build --preset build

# Run tests
ctest --preset test

# Clean
cmake --build --preset build --target clean
```

## Zed IDE

Install the [NeoCMake](https://github.com/neocmakelsp/neocmakelsp) extension for CMake language support.

To bind the project tasks defined in `.zed/tasks.json` to keyboard shortcuts, add the following to your local Zed keymap (`~/.config/zed/keymap.json`):

```json
[
  {
    "context": "Workspace",
    "bindings": {
      "ctrl-alt-cmd-b": ["task::Spawn", { "task_name": "CMake Build" }],
      "ctrl-alt-cmd-c": ["task::Spawn", { "task_name": "CMake Configure" }],
      "ctrl-alt-cmd-r": ["task::Spawn", { "task_name": "CMake Run Project" }],
      "ctrl-alt-cmd-t": ["task::Spawn", { "task_name": "CMake Tests" }]
    }
  }
]
```
