tree-sitter-gdscript
====================

GDScript grammar for [tree-sitter][].

- https://www.npmjs.com/package/tree-sitter-gdscript
- https://crates.io/crates/tree-sitter-gdscript

## Latest Godot Commit Syntactically Synced

Note: *Some commits may have been missed.*

```bash
git log --oneline --no-merges modules/gdscript
```

[6ae54fd787](https://github.com/godotengine/godot/commits/6ae54fd787)

## How To

- Test grammar
  1. `npm run genTest`
- Test scanner
  1. Edit "src/scanner.c"
  1. `npm run test`, no need to generate.
- Build prebuilds
  1. `npm run genTest`
  1. `npm run prebuild`
- Build with node-gyp
  1. `npm run genTest`
  1. `npm install node-gyp`
  1. `node-gyp rebuild`
- Edit
  1. Write tests in corpus to express behavior.
  1. Make grammar or scanner edits.
  1. See above for running tests.
  1. `npm run format`
  1. Commit changes.
     - If commit is an issue fix, prefix message with `fix #<issue-number>:`
     - List the rules changed in commit message.
     - Note what rules need to be updated in [nvim-treesitter][] queries.
  1. Commit generated files with the latest non-wip commit.
  1. Push
- Release
  1. Manually edit version in package files: CMakeLists.txt, Cargo.toml,
     Makefile, pyproject.toml, tree-sitter.json
  1. `npm version --git-tag-version false <major, minor, patch>`
  1. `git tag -a v<version>`
  1. `git push && git push --tags`
  1. `cargo package`
  1. `cargo publish`

Note: `node-gyp-build` will check for binaries in both `build` and `prebuilds`
directories.

[tree-sitter]: https://github.com/tree-sitter/tree-sitter
[nvim-treesitter]: https://github.com/nvim-treesitter/nvim-treesitter

## Notes on comment indentation

At the time of writing, the official GDScript parser built into Godot ignores comments and regions and does not have them as part of the code's parsed abstract syntax tree (AST).

In this parser, however, because it is used for syntax highlighting and folding support, we need to parse comments and regions and have them as part of the AST, which poses some challenges. The AST is meant to be a tree whose structure captures the nesting of the code.

Consider this snippet:

```gdscript
func test() -> void:
	if true:
	# Comment
		print("if body")
```

The call to the `print()` function is the body of the `if` statement. But where does the comment go? Is it part of the `if` statement's body or does it end the `if` statement's body and become its own sibling node?

By convention, comments that follow end the `if` statement's body are treated as belonging to the `if` statement's body. More generally, the indentation of comments and region markers is ignored by the parser. The code above will be parsed like this:

```scheme
(function_definition
  name: (name)
  parameters: (parameters)
  return_type: (type
    (identifier))
  body: (body
    (if_statement
      condition: (true)
      body: (body
        (comment)
        (expression_statement
          (call
            (identifier)
            arguments: (arguments
              (string)))))))
```

If you want to rebuild the indentation of comments and region markers, you can use the comment node's start column/byte offset in the source code and scan back to the previous line return character to determine the source indentation level.
