# fish completion for cjq.
#
# Install as /usr/share/fish/vendor_completions.d/cjq.fish.

complete -c cjq -s c -l compact -d 'Print compact JSON, one line, no spaces'
complete -c cjq -s r -l raw -d 'Print string results without quotes'
complete -c cjq -s j -l join-output -d 'Do not print a newline after each result'
complete -c cjq -s a -l ascii-output -d 'Escape non-ASCII characters as \uXXXX'
complete -c cjq -s s -l slurp -d 'Gather every JSON value into one array'
complete -c cjq -s e -l exit-status -d 'Exit 1 when the result is falsy or empty'
complete -c cjq -s n -l null-input -d 'Run the query against null, read no input'
complete -c cjq -s C -l color -d 'Force colors even when piped'
complete -c cjq -s M -l monochrome -d 'Never use colors'
complete -c cjq -l no-color -d 'Never use colors'
complete -c cjq -l indent -x -a '0 1 2 3 4 5 6 7 8' -d 'Spaces per indentation level'
complete -c cjq -l tab -d 'Indent with one tab'
complete -c cjq -s S -l sort-keys -d 'Sort object keys in the output'
complete -c cjq -s f -l from-file -r -d 'Read the query from a file'
complete -c cjq -s h -l help -d 'Show help'
complete -c cjq -s v -l version -d 'Show version'
complete -c cjq -F
