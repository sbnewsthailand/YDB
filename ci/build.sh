#!/usr/bin/env bash

# Exit on error; disallow unset variables; print each command as it is executed
set -euv
# If any command in a pipeline fails, count the entire pipeline as failed
set -o pipefail

# This is quick to execute, so do it first without having to wait for a full build
echo "# Check for a commit that was not gpg-signed"
# If you need to add a new key ID, add it to the below list.
# The key will also have to be on a public key server.
# If you have never published it before, you can do so with
# `gpg --keyserver hkps://keyserver.ubuntu.com --send-keys KEY_ID`.
# You can find your key ID with `gpg -K`;
# upload all keys that you will use to sign commits.
GPG_KEYS=(
    # Joshua Nelson
    "A7AC371F484CB10A0EC1CAE1964534138B7FB42E"
    # Jon Badiali
    "58FE3050D9F48C46FC506C68C5C2A5A682CB4DDF"
    # Brad Westhafer
    "FC859B0401A6C5F92BF1C1061E46090B0FD0A34E"
    # Jaahanavee Sikri
    "192D7DD0968178F057B2105DE5E6FCCE1994F0DD"
    # Narayanan Iyer
    "0CE9E4C2C6E642FE14C1BB9B3892ED765A912982"
    # Ganesh Mahesh
    "74774D6D0DB17665AB75A18211A87E9521F6FB86"
    # K S Bhaskar
    "D3CFECF187AECEAA67054719DCF03D8B30F73415"
    # Steven Estess
    "CC9A13F429C7F9231E4DFB2832655C57E597CF83"
)
gpg --keyserver hkps://keyserver.ubuntu.com --recv-keys "${GPG_KEYS[@]}"
# verify-commit was only introduced in 2.5: https://stackoverflow.com/a/32038784/7669110
# If git is not recent enough for the check, just skip it
MAJOR_GIT_VERSION="$(git --version | cut -d ' ' -f 3 | cut -d '.' -f 1)"
if [ $MAJOR_GIT_VERSION -ge 2 ] && ! git verify-commit HEAD; then
    echo " -> The commit was not signed with a known GPG key!"
    exit 1
fi

echo "# Randomly choose to build Debug or Release build"
if [[ $(( $RANDOM % 2)) -eq 0 ]]; then
	build_type="Debug"
else
	build_type="RelWithDebInfo"
fi
echo " -> build_type = $build_type"

echo "# Run the build using clang"
mkdir build
cd build
cmake -D CMAKE_C_COMPILER=clang-10 -D CMAKE_BUILD_TYPE=$build_type -D CMAKE_EXPORT_COMPILE_COMMANDS=ON ..
# Record the warnings, but if `make` fails, say why instead of silently existing.
set +e
make -j $(nproc) 2>make_warnings.txt
status=$?
set -e
if ! [ $status = 0 ]; then
	echo "# make failed with exit status [$status]. make output follows below"
	cat make_warnings.txt
	exit $status
fi

# Check for unexpected warnings
../ci/sort_warnings.sh make_warnings.txt

# This is used for both `make` and `clang-tidy` warnings.
compare() {
	expected="$1"
	actual="$2"
	# We do not want any failures in "diff" command below to exit the script (we want to see the actual diff a few steps later).
	# So never count this step as failing even if the output does not match.
	diff "$expected" sorted_warnings.txt &> differences.txt || true

	if [ $(wc -l differences.txt | awk '{print $1}') -gt 0 ]; then
		echo " -> Expected warnings differ from actual warnings! diff output follows"
		echo " -> note: '<' indicates an expected warning, '>' indicates an actual warning"
		cat differences.txt
		echo
		echo " -> note: these were the original warnings:"
		grep -Ev '^(Database file |%YDB-I-DBFILECREATED, |%GDE-I-|YDB-MUMPS\[)' make_warnings.txt \
			| grep -v '\.gld$'
		echo
		echo " -> note: the expected warnings are available in $expected"
		exit 1
	fi
}
if [ -e ../ci/warnings.ref ]; then
	compare ../ci/warnings.ref sorted_warnings.txt
else
	# Make sure that there are actually no warnings, not just that someone forgot to add a .ref file
	[ $(wc -c < sorted_warnings.txt) = 0 ]
fi

# TODO(#615): add clang-tidy warnings in a follow-up MR
