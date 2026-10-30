package com.mcpe.sunch.client0143;

import android.annotation.TargetApi;
import android.app.Activity;
import android.hardware.input.InputManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import android.view.InputDevice;
import android.view.View;
import android.view.ViewGroup;

/** Owns Android pointer capture. NativeActivity's InputQueue owns mouse events. */
public final class MouseInput implements InputManager.InputDeviceListener {
    private final Activity activity;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final InputManager devices;
    private boolean running, hasMouse, wasCaptured, bridgeAvailable = true;
    private View captureView;
    private long lastRequest;

    public MouseInput(Activity activity) {
        this.activity = activity;
        devices = (InputManager) activity.getSystemService(Activity.INPUT_SERVICE);
    }

    public void resume() {
        if (running) return;
        running = true;
        if (devices != null) devices.registerInputDeviceListener(this, handler);
        updateDevices();
        handler.post(poll);
    }

    public void pause() {
        if (!running) return;
        running = false;
        handler.removeCallbacks(poll);
        if (devices != null) devices.unregisterInputDeviceListener(this);
        publishWindow(false);
        releaseCapture();
    }

    public void onWindowFocusChanged(boolean focused) {
        publishWindow(running && focused);
        if (!focused) releaseCapture();
        else if (running) {
            handler.removeCallbacks(poll);
            handler.post(poll);
        }
    }

    private void updateDevices() {
        hasMouse = false;
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice device = InputDevice.getDevice(id);
            if (device != null && device.supportsSource(InputDevice.SOURCE_MOUSE)) {
                hasMouse = true;
                break;
            }
        }
        publishWindow(running && activity.hasWindowFocus());
    }

    private void publishWindow(boolean focused) {
        if (!bridgeAvailable) return;
        try {
            nativeWindowState(focused, hasMouse);
        } catch (UnsatisfiedLinkError error) {
            bridgeAvailable = false;
            Log.w("SunchMouse", "Native mouse bridge unavailable", error);
        }
    }

    private void releaseCapture() {
        if (Build.VERSION.SDK_INT >= 26 && captureView != null) Api26.release(captureView);
        captureView = null;
        wasCaptured = false;
        lastRequest = 0;
    }

    private View focusedGameView() {
        View view = activity.getWindow().getDecorView().findFocus();
        if (view != null) return view;
        view = activity.findViewById(android.R.id.content);
        if (view instanceof ViewGroup && ((ViewGroup) view).getChildCount() > 0) {
            view = ((ViewGroup) view).getChildAt(0);
        }
        if (view == null) return null;
        view.setFocusable(true);
        view.setFocusableInTouchMode(true);
        return view.requestFocus() ? view : null;
    }

    private final Runnable poll = new Runnable() {
        @Override public void run() {
            if (!running || !bridgeAvailable) return;
            try {
                int state = nativeState();
                boolean wanted = (state & 3) == 3 && activity.hasWindowFocus();
                if (Build.VERSION.SDK_INT >= 26) {
                    boolean captured = captureView != null && Api26.captured(captureView);
                    if (wasCaptured && !captured && wanted) {
                        // Respect an OS/user capture release. The native adapter
                        // re-arms on a fresh click or a new gameplay context.
                        nativeCaptureLost();
                        wanted = false;
                    }
                    if (!wanted) {
                        releaseCapture();
                    } else {
                        View focused = focusedGameView();
                        if (focused != null && focused != captureView) {
                            releaseCapture();
                            captureView = focused;
                        }
                        if (captureView != null && !Api26.captured(captureView)
                                && SystemClock.uptimeMillis() - lastRequest >= 300) {
                            lastRequest = SystemClock.uptimeMillis();
                            Api26.capture(captureView);
                        }
                        wasCaptured = captureView != null && Api26.captured(captureView);
                    }
                }
            } catch (UnsatisfiedLinkError error) {
                bridgeAvailable = false;
                releaseCapture();
                Log.w("SunchMouse", "Native mouse bridge unavailable", error);
            } catch (RuntimeException error) {
                // A disappearing window can reject a request during a screen
                // transition. Let the next focused poll retry.
                releaseCapture();
                Log.w("SunchMouse", "Pointer capture request rejected", error);
            }
            if (running && bridgeAvailable) handler.postDelayed(this, 40);
        }
    };

    @Override public void onInputDeviceAdded(int id) { updateDevices(); }
    @Override public void onInputDeviceRemoved(int id) { updateDevices(); }
    @Override public void onInputDeviceChanged(int id) { updateDevices(); }

    @TargetApi(26)
    private static final class Api26 {
        static boolean captured(View view) { return view.hasPointerCapture(); }
        static void capture(View view) { view.requestPointerCapture(); }
        static void release(View view) { if (view.hasPointerCapture()) view.releasePointerCapture(); }
    }

    private static native int nativeState();
    private static native void nativeWindowState(boolean focused, boolean connected);
    private static native void nativeCaptureLost();
}
