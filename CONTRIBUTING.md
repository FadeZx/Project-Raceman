# Contributing Workflow Tips

When you receive a patch (for example, from `git format-patch` output or from a code review system), you do not need to open a pull request to try it locally. Instead, you can apply it directly to your current branch:

```bash
git apply /path/to/patch.diff
```

After running `git apply`, inspect the changes with `git status` and `git diff`. If the patch applies cleanly and looks correct, you can commit it as usual.

Use a pull request when you want to share your committed changes with others for review or integration. Applying a patch locally is purely for updating your working tree without the overhead of a PR.

If the patch was produced with `git format-patch`, you can also use `git am` to preserve author information:

```bash
git am /path/to/patch.patch
```

Choose the approach that best fits the source of the patch and whether you need review or collaboration.
