#!/bin/sh
# Simulates pasting the base64 text through a browser (line endings changed, trailing blank lines),
# then decodes it the way the Pi would and applies it with git to a clean upstream tree
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq git >/dev/null 2>&1
rm -rf /t && mkdir -p /t/base /t/want && cd /t

# worst case paste: every line ending turned into CRLF, extra blank lines at the end
sed 's/$/\r/' /work/pi3-60fps.patch.b64 > pasted.txt
printf '\r\n\r\n' >> pasted.txt

base64 -di pasted.txt | gunzip > decoded.patch
if cmp -s decoded.patch /work/pi3-60fps.patch; then echo "DECODE OK: identical to the original patch"; else echo "DECODE MISMATCH"; exit 1; fi

tar -xf /work/base.tar -C /t/base
tar -xf /work/patched.tar -C /t/want
cd /t/base
git init -q . && git add -A >/dev/null 2>&1 && git -c user.name=t -c user.email=t@t commit -q -m base >/dev/null
if git apply /t/decoded.patch 2>/t/apply.err; then echo "GIT APPLY OK"; else echo "GIT APPLY FAILED"; head -5 /t/apply.err; exit 1; fi
grep -c . /t/apply.err | sed 's/^/git apply warning lines: /'
rm -rf .git
if diff -r /t/base /t/want >/dev/null; then echo "RESULT MATCHES THE TESTED TREE"; else echo "RESULT DIFFERS"; diff -rq /t/base /t/want | head; fi
