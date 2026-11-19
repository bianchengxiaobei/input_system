package com.insys.input;

public final class MobileInput {
    static {
        System.loadLibrary("insys_android_jni");
    }

    public static final int PHASE_BEGAN = 0;
    public static final int PHASE_MOVED = 1;
    public static final int PHASE_STATIONARY = 2;
    public static final int PHASE_ENDED = 3;
    public static final int PHASE_CANCELED = 4;

    public static final int SENSOR_ACCELEROMETER = 1;
    public static final int SENSOR_MAGNETIC_FIELD = 2;
    public static final int SENSOR_GYROSCOPE = 4;
    public static final int SENSOR_LIGHT = 5;
    public static final int SENSOR_PRESSURE = 6;
    public static final int SENSOR_PROXIMITY = 8;
    public static final int SENSOR_GRAVITY = 9;
    public static final int SENSOR_LINEAR_ACCELERATION = 10;
    public static final int SENSOR_ROTATION_VECTOR = 11;
    public static final int SENSOR_RELATIVE_HUMIDITY = 12;
    public static final int SENSOR_AMBIENT_TEMPERATURE = 13;
    public static final int SENSOR_GAME_ROTATION_VECTOR = 15;
    public static final int SENSOR_STEP_COUNTER = 19;
    public static final int SENSOR_HINGE_ANGLE = 36;

    private MobileInput() {}

    public static void pushTouch(int id, float x, float y, float pressure, int phase) {
        nativePushTouch(id, x, y, pressure, phase);
    }

    public static void pushSensor(int type, float[] values) {
        nativePushSensor(type, values);
    }

    public static void suspend() {
        nativeSuspend();
    }

    private static native void nativePushTouch(int id, float x, float y,
                                               float pressure, int phase);
    private static native void nativePushSensor(int type, float[] values);
    private static native void nativeSuspend();
}

