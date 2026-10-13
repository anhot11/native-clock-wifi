#!/bin/sh

# Gradle start-up script for POSIX systems
APP_BASE_NAME=`basename "$0"`
APP_HOME="`pwd -P`"

# Use default java if available
if [ -n "$JAVA_HOME" ] ; then
    JAVACMD="$JAVA_HOME/bin/java"
else
    JAVACMD="java"
fi

# Locate gradle-wrapper.jar
WRAPPER_JAR="$APP_HOME/gradle/wrapper/gradle-wrapper.jar"

# If wrapper jar doesn't exist, download or run gradle
if [ ! -f "$WRAPPER_JAR" ]; then
    echo "Downloading Gradle Wrapper..."
    mkdir -p "$APP_HOME/gradle/wrapper"
    curl -sLo "$WRAPPER_JAR" https://raw.githubusercontent.com/gradle/gradle/v8.11.1/gradle/wrapper/gradle-wrapper.jar || true
fi

exec "$JAVACMD" -jar "$WRAPPER_JAR" "$@"
