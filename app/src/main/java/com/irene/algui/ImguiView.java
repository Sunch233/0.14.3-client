package com.irene.algui;

import android.app.Activity;
import android.content.Context;
import android.graphics.PixelFormat;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.WindowManager;

import androidx.annotation.NonNull;

public class ImguiView extends SurfaceView implements SurfaceHolder.Callback {

    private static ImguiView instance;
    private static WindowManager mWindowManager;
    private static final Handler mMainHandler = new Handler(Looper.getMainLooper());

    public static void show(final Context ctx) {
        if (!(ctx instanceof Activity)) {
            return;
        }

        final Activity activity = (Activity) ctx;
        IMEHelper.init(activity);
        if (activity.isFinishing() || activity.isDestroyed()) return;

        mMainHandler.post(() -> {
            if (instance != null) return;
            try {
                mWindowManager = (WindowManager) activity.getSystemService(Context.WINDOW_SERVICE);
                if (mWindowManager == null) return;

                instance = new ImguiView(activity);
                WindowManager.LayoutParams params = new WindowManager.LayoutParams();
                params.width = WindowManager.LayoutParams.MATCH_PARENT;
                params.height = WindowManager.LayoutParams.MATCH_PARENT;
                params.type = WindowManager.LayoutParams.TYPE_APPLICATION;

                params.flags = WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE
                        | WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                        | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN
                        |WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS;

                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {

                    params.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
                }
                params.format = PixelFormat.TRANSLUCENT;
                params.gravity = Gravity.CENTER;
                params.dimAmount = 0f;
                mWindowManager.addView(instance, params);

            } catch (Exception ignored) {
                instance = null;
                mWindowManager = null;
            }
        });
    }

    public static void hide() {
        mMainHandler.post(() -> {
            if (mWindowManager != null && instance != null) {
                try {
                    mWindowManager.removeViewImmediate(instance);
                } catch (Exception ignored) {}
            }
            mWindowManager = null;
            instance = null;
        });
    }

    public ImguiView(Context context) {
        super(context);
        getHolder().addCallback(this);
        setZOrderOnTop(true);
        getHolder().setFormat(PixelFormat.TRANSLUCENT);
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        ImguiSurfaceCreated(holder.getSurface());
    }

    @Override
    public void surfaceChanged(@NonNull SurfaceHolder h, int f, int w, int h1) {
        ImguiSurfaceChanged(w, h1);
    }

    @Override
    public void surfaceDestroyed(@NonNull SurfaceHolder holder) {
        ImguiSurfaceDestroyed();
    }

    private native void ImguiSurfaceCreated(Surface surface);
    private native void ImguiSurfaceDestroyed();
    private native void ImguiSurfaceChanged(int width, int height);
}