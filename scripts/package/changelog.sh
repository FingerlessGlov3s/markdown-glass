#!/usr/bin/env bash
# Turns CHANGELOG.md into the formats each package needs. CHANGELOG.md is the
# only place release notes are written.
#
#   changelog.sh rpm                  %changelog entries for the RPM spec
#   changelog.sh debian <suffix>      debian/changelog text; <suffix> follows
#                                     the version, e.g. "1~debian13"
#   changelog.sh metainfo             AppStream <release> elements
#   changelog.sh notes <version>      the Markdown body of one release
#   changelog.sh latest               the newest released version
#
# Only dated releases are used; the [Unreleased] section is skipped. Needs
# nothing but bash, awk and GNU date, so it runs in every build container.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CHANGELOG=${CHANGELOG:-$ROOT/CHANGELOG.md}
MAINTAINER="FingerlessGloves <me@fingerlessgloves.me>"

# Emits one record per item: "R<TAB>version<TAB>date" for each release, then
# "P<TAB>text" for a summary line and "B<TAB>text" for each bullet (wrapped
# lines joined). A release heading that is not exactly "## [version] - date"
# (trailing text such as "(yanked)", a one-digit month) is an error: the
# format is documented at the top of CHANGELOG.md, and silently trimming
# would publish something the author did not mean to.
parse() {
    local records
    records=$(awk '
        function flush() { if (item != "") { print kind "\t" item; item = "" } }
        /^## \[[^]]+\] - [0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]$/ {  # no {n}: mawk lacks it
            flush()
            version = $0; sub(/^## \[/, "", version); sub(/\].*/, "", version)
            date = $0; sub(/.* - /, "", date)
            inrelease = 1
            print "R\t" version "\t" date
            next
        }
        /^## \[/ && !/^## \[Unreleased\]$/ {
            print FILENAME ":" NR ": release heading must be \"## [version] - YYYY-MM-DD\": " $0 > "/dev/stderr"
            bad = 1
            exit 1
        }
        /^## / { flush(); inrelease = 0; next }
        !inrelease { next }
        /^- / { flush(); kind = "B"; item = substr($0, 3); next }
        /^  [^ ]/ && item != "" { line = $0; sub(/^ +/, "", line); item = item " " line; next }
        /^[[:space:]]*$/ { flush(); next }
        { flush(); kind = "P"; item = $0 }
        END { if (!bad) flush() }
    ' "$CHANGELOG") || return 1 # explicit: a command substitution does not inherit set -e
    # The pattern cannot tell a 13th month from a real one; GNU date can, and
    # a bad date must stop the build rather than leave a blank in a changelog.
    local kind version date
    while IFS=$'\t' read -r kind version date; do
        [[ $kind == R ]] || continue
        if ! LC_ALL=C date -u -d "$date" >/dev/null 2>&1; then
            echo "$CHANGELOG: release $version has an invalid date: $date" >&2
            return 1
        fi
    done <<<"$records"
    printf '%s\n' "$records"
}

# Prints text after `first` (the first line's prefix), wrapped at 79 columns
# with following lines indented to line up under it.
wrap() {
    local first=$1 text=$2 indent
    indent=$(printf '%*s' "${#first}" '')
    printf '%s\n' "$text" | fold -s -w $((79 - ${#first})) | sed -e 's/ *$//' -e "1s/^/$first/" -e "1!s/^/$indent/"
}

# The replacements are quoted on purpose: from bash 5.2 an unquoted & in a
# ${var//pattern/replacement} stands for the matched text, so an unquoted
# &lt; would turn "<" into "<lt;".
xml_escape() {
    local s=$1
    s=${s//&/'&amp;'}
    s=${s//</'&lt;'}
    s=${s//>/'&gt;'}
    printf '%s' "$s"
}

case "${1:-}" in
rpm | debian | metainfo | notes | latest) ;;
*)
    sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
    ;;
esac

# Parsed once, up front: a malformed changelog must fail the command, and the
# exit status of a process substitution (`< <(parse)`) would be ignored.
records=$(parse) || exit 1

case "$1" in
rpm)
    first=1
    while IFS=$'\t' read -r kind a b; do
        case $kind in
        R)
            if ((!first)); then echo; fi
            first=0
            stamp=$(LC_ALL=C date -u -d "$b" '+%a %b %d %Y')
            echo "* $stamp $MAINTAINER - $a-1"
            ;;
        P | B) wrap "- " "${a//%/%%}" ;; # a lone % would be read as an RPM macro
        esac
    done <<<"$records"
    ;;
debian)
    suffix=${2:?usage: changelog.sh debian <suffix>}
    open=0
    footer=""
    while IFS=$'\t' read -r kind a b; do
        case $kind in
        R)
            if ((open)); then printf '\n%s\n\n' "$footer"; fi
            open=1
            echo "markdown-glass ($a-$suffix) unstable; urgency=medium"
            echo
            stamp=$(LC_ALL=C date -u -d "$b" '+%a, %d %b %Y %H:%M:%S +0000')
            footer=" -- $MAINTAINER  $stamp"
            ;;
        P | B) wrap "  * " "$a" ;;
        esac
    done <<<"$records"
    if ((open)); then printf '\n%s\n' "$footer"; fi
    ;;
metainfo)
    open=0
    list=0
    while IFS=$'\t' read -r kind a b; do
        case $kind in
        R)
            if ((list)); then echo "        </ul>"; fi
            if ((open)); then printf '      </description>\n    </release>\n'; fi
            open=1
            list=0
            printf '    <release version="%s" date="%s">\n      <description>\n' "$a" "$b"
            ;;
        P)
            if ((list)); then echo "        </ul>"; fi
            list=0
            echo "        <p>$(xml_escape "$a")</p>"
            ;;
        B)
            if ((!list)); then echo "        <ul>"; fi
            list=1
            echo "          <li>$(xml_escape "$a")</li>"
            ;;
        esac
    done <<<"$records"
    if ((list)); then echo "        </ul>"; fi
    if ((open)); then printf '      </description>\n    </release>\n'; fi
    ;;
notes)
    want=${2:?usage: changelog.sh notes <version>}
    found=0
    while IFS=$'\t' read -r kind a b; do
        case $kind in
        R)
            if [[ "$a" == "$want" ]]; then
                found=1
            elif ((found)); then
                break
            fi
            ;;
        P) if ((found)); then printf '%s\n\n' "$a"; fi ;;
        B) if ((found)); then echo "- $a"; fi ;;
        esac
    done <<<"$records"
    if ((!found)); then
        echo "No release $want in CHANGELOG.md" >&2
        exit 1
    fi
    ;;
latest)
    awk -F'\t' '$1 == "R" { print $2; exit }' <<<"$records"
    ;;
esac
