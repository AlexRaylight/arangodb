#!/bin/bash
export PID=$$
mkdir data-$PID
export SCRIPT=$1
shift
export PORT=`expr 1024 + $RANDOM`
declare -a ARGS
export VG=''
export VXML=''
for i in "$@"; do
    if test "$i" == valgrind; then
      export VG='\usr\bin\valgrind --log-file=\tmp\valgrindlog.%p'
    elif test "$i" == valgrindxml; then
      export VG='\usr\bin\valgrind --xml=yes --xml-file=valgrind_testrunner'
      export VXML="valgrind=\"${VG}\""
      export VG=${VG}'.xml '
    else
      ARGS+=(--javascript.script-parameter)
      ARGS+=("$i")
    fi
done
echo Database has its data in data-$PID
echo Logfile is in log-$PID
$VG bin/arangod --configuration none --cluster.agent-path 'bin\etcd-arango' --cluster.arangod-path 'bin\arangod' --cluster.coordinator-config 'etc\relative\arangod-coordinator.conf' --cluster.dbserver-config 'etc\relative\arangod-dbserver.conf' --cluster.disable-dispatcher-frontend false --cluster.disable-dispatcher-kickstarter false --cluster.data-path cluster --cluster.log-path cluster --database.directory data-$PID --log.file log-$PID --server.endpoint tcp://localhost:$PORT --javascript.startup-directory js --javascript.app-path 'js\apps' --javascript.script $SCRIPT --ruby.action-directory 'mr\actions\system' --ruby.modules-path 'mr\server\modules;mr\common\modules' "${ARGS[@]}" --temp-path '\var\tmp\' $VXML


if test $? -eq 0; then
  echo removing log-$PID data-$PID
  rm -rf log-$PID data-$PID
else
  echo "failed - don't remove log-$PID data-$PID"
fi

echo Server has terminated.
