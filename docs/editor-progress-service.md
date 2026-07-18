# Editor Progress Windows

Use `EditorProgressService` for any editor operation that may take long enough for the user to wonder whether the editor is working. The service owns one Unity-style modal progress window and is safe to update from worker threads.

## Background operation

```cpp
EditorProgressTask task = EditorProgressService::Get().Begin(
    "Importing Model", "Processing meshes and materials...", meshCount, true);

worker = std::thread([task, meshCount]() mutable {
    for (int index = 0; index < meshCount; ++index) {
        if (task.IsCancellationRequested()) {
            task.Cancelled();
            return;
        }
        task.SetProgress(index, meshCount);
        task.SetDetail("Importing mesh " + std::to_string(index + 1));
        // Process one item.
    }
    task.Complete("Import complete.");
});
```

Call `Fail(message)` when an operation cannot complete. Failed tasks remain visible until the user closes them. Successful and cancelled tasks close automatically after briefly showing their result.

## Synchronous main-thread operation

A modal cannot appear while the main thread is blocked. Start the task first, wait until `HasBeenRendered()` returns true on a later frame, then perform the synchronous work and call `Complete()` or `Fail()`. Scene and project saving use this pattern.

`ScopedEditorProgress` is intended only for short operations where brief completion feedback is sufficient; it cannot animate while synchronous work blocks the UI thread.
