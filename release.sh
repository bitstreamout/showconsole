#!/bin/bash
if ! git diff-index --quiet HEAD --
then
    echo "Error: uncommitted changes. PLEASE commit or stash your changes." 1>&2
    exit 1
fi
BRANCH=$(git symbolic-ref --short HEAD)

CSTAG=$(git describe --tags --abbrev=0 2>/dev/null || echo "v0.0")
CVTAG=($(echo "${CSTAG}" | sed "s/^v// ; s/\./ /g"))

echo "Current version tag: ${CSTAG}"
read -p "Provide a new version tag: " STAG

VTAG=($(echo "${STAG}" | sed "s/^v// ; s/\./ /g")) || exit 1
if test ${#VTAG[@]} -ne 2 -o "${STAG:0:1}" != v
then
    echo "Error: unexpected version string ${STAG}, use format vMAJOR.MINOR" 1>&2
    exit 1
fi
if test "${VTAG[0]}" -lt "${CVTAG[0]}" || \
	test "${VTAG[0]}" -eq "${CVTAG[0]}" -a "${VTAG[1]}" -le "${CVTAG[1]}"
then
    echo "Error: New version v${VTAG[0]}.${VTAG[1]} must be higher than ${CSTAG}!" 1>&2
    exit 1
fi
if git rev-parse "$STAG" >/dev/null 2>&1
then
    echo "Error: Tag ${STAG} already exists!" 1>&2
    exit 1
fi
echo -n "Final new version tag: "
echo v${VTAG[0]}.${VTAG[1]}
echo "Do you wish to commit and push this?"
select strictreply in "Yes" "No"; do
    relaxedreply=${strictreply:-$REPLY}
    case $relaxedreply in
	Yes|yes|y)
	    break;;
	No|no|n)
	    exit 0;;
	*)
	    exit 1;;
    esac
done
sed -ri "s/^(MAJOR[[:space:]]*:=[[:space:]]*)[0-9]+/\1${VTAG[0]}/;s/^(MINOR[[:space:]]*:=[[:space:]]*)[0-9]+/\1${VTAG[1]}/" Makefile || exit 1
git add Makefile
git commit -m "Release: bump version to ${VTAG[0]}.${VTAG[1]}"
git tag v${VTAG[0]}.${VTAG[1]}
git push origin "${BRANCH:-master}" --tags
