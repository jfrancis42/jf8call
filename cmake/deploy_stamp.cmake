# deploy_stamp.cmake
# Called by the jf8call POST_BUILD custom command to scp the build stamp file
# to the ordo web server.  Failures are silently ignored (no server access on
# developer machines or in CI).
#
# Usage (from CMakeLists.txt):
#   cmake -DSTAMP_FILE=<path> -P deploy_stamp.cmake

execute_process(
    COMMAND scp -q -o StrictHostKeyChecking=no -o ConnectTimeout=5
            "${STAMP_FILE}"
            jfrancis@ordo:/var/www/ordo/jf8call-build-time
    RESULT_VARIABLE _result
    OUTPUT_QUIET
    ERROR_QUIET
)
# Ignore failures — no server access is normal on dev/CI machines.
