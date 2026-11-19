package com.insys.input;

import android.content.Context;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.view.MotionEvent;

import java.util.BitSet;

public final class AndroidInput {
    private static SensorManager sSensorManager;
    private static SensorEventListener sSensorListener;
    private static final BitSet sActivePointers = new BitSet(32);

    private AndroidInput() {}

    public static boolean startSensors(Context context) {
        stopSensors();
        if (context == null) {
            return false;
        }
        Context app = context.getApplicationContext();
        if (app == null) {
            app = context;
        }
        SensorManager manager =
                (SensorManager) app.getSystemService(Context.SENSOR_SERVICE);
        if (manager == null) {
            return false;
        }

        SensorEventListener listener = new SensorEventListener() {
            @Override
            public void onSensorChanged(SensorEvent event) {
                MobileInput.pushSensor(event.sensor.getType(), event.values);
            }

            @Override
            public void onAccuracyChanged(Sensor sensor, int accuracy) {}
        };

        int registered = 0;
        registered += register(manager, listener, Sensor.TYPE_ACCELEROMETER);
        registered += register(manager, listener, Sensor.TYPE_GYROSCOPE);
        registered += register(manager, listener, Sensor.TYPE_LIGHT);
        registered += register(manager, listener, Sensor.TYPE_PRESSURE);
        registered += register(manager, listener, Sensor.TYPE_PROXIMITY);
        registered += register(manager, listener, Sensor.TYPE_RELATIVE_HUMIDITY);
        registered += register(manager, listener, Sensor.TYPE_AMBIENT_TEMPERATURE);
        registered += register(manager, listener, Sensor.TYPE_STEP_COUNTER);
        registered += register(manager, listener, Sensor.TYPE_HINGE_ANGLE);
        registered += register(manager, listener, Sensor.TYPE_GRAVITY);
        registered += register(manager, listener, Sensor.TYPE_LINEAR_ACCELERATION);
        registered += register(manager, listener, Sensor.TYPE_MAGNETIC_FIELD);
        final int rotation = register(manager, listener,
                Sensor.TYPE_ROTATION_VECTOR);
        if (rotation != 0) {
            registered += rotation;
        } else {
            registered += register(manager, listener,
                    Sensor.TYPE_GAME_ROTATION_VECTOR);
        }

        if (registered == 0) {
            return false;
        }
        sSensorManager = manager;
        sSensorListener = listener;
        return true;
    }

    public static void stopSensors() {
        if (sSensorManager != null && sSensorListener != null) {
            sSensorManager.unregisterListener(sSensorListener);
        }
        sSensorManager = null;
        sSensorListener = null;
    }

    public static boolean handleMotionEvent(MotionEvent event) {
        if (event == null) {
            return false;
        }
        switch (event.getActionMasked()) {
        case MotionEvent.ACTION_DOWN:
        case MotionEvent.ACTION_POINTER_DOWN: {
            final int index = event.getActionIndex();
            final int id = event.getPointerId(index);
            sActivePointers.set(id);
            MobileInput.pushTouch(id, event.getX(index), event.getY(index),
                    event.getPressure(index), MobileInput.PHASE_BEGAN);
            return true;
        }
        case MotionEvent.ACTION_MOVE:
            for (int i = 0; i < event.getPointerCount(); ++i) {
                final int id = event.getPointerId(i);
                final int phase = sActivePointers.get(id)
                        ? MobileInput.PHASE_MOVED
                        : MobileInput.PHASE_BEGAN;
                sActivePointers.set(id);
                MobileInput.pushTouch(id, event.getX(i), event.getY(i),
                        event.getPressure(i), phase);
            }
            return true;
        case MotionEvent.ACTION_POINTER_UP:
        case MotionEvent.ACTION_UP: {
            final int index = event.getActionIndex();
            final int id = event.getPointerId(index);
            sActivePointers.clear(id);
            MobileInput.pushTouch(id, event.getX(index), event.getY(index),
                    event.getPressure(index), MobileInput.PHASE_ENDED);
            return true;
        }
        case MotionEvent.ACTION_CANCEL:
            for (int i = 0; i < event.getPointerCount(); ++i) {
                final int id = event.getPointerId(i);
                if (!sActivePointers.get(id)) {
                    continue;
                }
                sActivePointers.clear(id);
                MobileInput.pushTouch(id, event.getX(i), event.getY(i),
                        event.getPressure(i), MobileInput.PHASE_CANCELED);
            }
            sActivePointers.clear();
            return true;
        default:
            return false;
        }
    }

    public static void suspend() {
        sActivePointers.clear();
        MobileInput.suspend();
    }

    private static int register(SensorManager manager,
                                SensorEventListener listener, int type) {
        Sensor sensor = manager.getDefaultSensor(type);
        if (sensor == null) {
            return 0;
        }
        return manager.registerListener(
                listener, sensor, SensorManager.SENSOR_DELAY_GAME) ? 1 : 0;
    }
}

