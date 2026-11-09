# bash completion for cjq.
#
# Install as /usr/share/bash-completion/completions/cjq (or source it from
# ~/.bashrc).  cjq's first positional argument is a free-form query, so only
# flags and file names are completed.

_cjq()
{
    local cur prev opts
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"

    case "$prev" in
        --indent)
            COMPREPLY=( $(compgen -W "0 1 2 3 4 5 6 7 8" -- "$cur") )
            return 0
            ;;
        -f|--from-file)
            COMPREPLY=( $(compgen -f -- "$cur") )
            return 0
            ;;
    esac

    if [[ $cur == -* ]]; then
        opts="-c --compact -r --raw -j --join-output -a --ascii-output \
-s --slurp -e --exit-status -n --null-input -C --color -M --monochrome \
--no-color --indent --tab -S --sort-keys -f --from-file -h --help -v --version"
        COMPREPLY=( $(compgen -W "$opts" -- "$cur") )
        return 0
    fi

    if (( COMP_CWORD <= 2 )); then
        COMPREPLY=( $(compgen -f -- "$cur") )
    fi
    return 0
}

complete -F _cjq cjq
