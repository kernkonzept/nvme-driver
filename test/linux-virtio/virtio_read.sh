#!/bin/sh

echo 'TAP TEST START'
echo '1..1'
if [ -e /dev/vda ]; then
  echo "2f0435272b6ed3614a0abc90a74e0fc1  /dev/vda" | md5sum -cs
  if [ $? == 0 ]; then
    echo 'ok 1 - md5 sum correct'
  else
    cat << ERROROUT
not ok 1 - bad md5sum
# expected: 2f0435272b6ed3614a0abc90a74e0fc1
# got:      $(md5sum /dev/vda)
ERROROUT
  fi
else
  echo 'not ok 1 - /dev/vda does not exist.'
fi
echo 'TAP TEST FINISH'
