package com.mcpe.sunch.client0143;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.graphics.Typeface;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;

/** Android presentation only; resource diagnostics and persistence live in features/. */
public final class ErrorReports {
    private static final int SAVE_REPORT = 0x5345;
    private static final int MAX_REPORT_BYTES = 384 * 1024;
    private final Activity activity;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private boolean running, bridgeAvailable = true;
    private AlertDialog dialog;
    private String exportText;

    public ErrorReports(Activity activity) { this.activity = activity; }

    // Call before NativeActivity.onCreate starts the native game thread.
    public static void configure(Activity activity) {
        nativeConfigure(new File(activity.getFilesDir(), "error-reports").getAbsolutePath());
    }

    public void resume() {
        if (running) return;
        running = true;
        handler.post(poll);
    }

    public void pause() {
        running = false;
        handler.removeCallbacks(poll);
    }

    public void destroy() {
        pause();
        // Destroying an activity does not acknowledge its pending error.
        if (dialog != null) { dialog.dismiss(); dialog = null; }
    }

    private final Runnable poll = new Runnable() {
        @Override public void run() {
            if (!running || !bridgeAvailable) return;
            try {
                if (dialog == null && activity.hasWindowFocus() && !activity.isFinishing()) {
                    String receipt = nativePending();
                    if (receipt != null) show(receipt);
                }
            } catch (UnsatisfiedLinkError error) {
                bridgeAvailable = false;
                Log.w("SunchErrors", "Native report bridge unavailable", error);
            } catch (IOException error) {
                Log.e("SunchErrors", "Cannot read resource error report", error);
            }
            if (running && bridgeAvailable) handler.postDelayed(this, 1000);
        }
    };

    private void show(String receipt) throws IOException {
        int separator = receipt.indexOf('\n');
        if (separator < 0) return;
        File file = new File(receipt.substring(0, separator));
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (FileInputStream input = new FileInputStream(file)) {
            byte[] buffer = new byte[4096];
            int count;
            while (bytes.size() < MAX_REPORT_BYTES &&
                    (count = input.read(buffer, 0, Math.min(buffer.length, MAX_REPORT_BYTES - bytes.size()))) > 0) {
                bytes.write(buffer, 0, count);
            }
        }
        final String report = new String(bytes.toByteArray(), StandardCharsets.UTF_8);
        TextView text = new TextView(activity);
        text.setText("报告已保存：" + file.getAbsolutePath() + "\n\n" + report);
        text.setTextIsSelectable(true);
        text.setTextSize(12);
        text.setTypeface(Typeface.MONOSPACE);
        int padding = (int) (16 * activity.getResources().getDisplayMetrics().density);
        text.setPadding(padding, padding, padding, padding);
        ScrollView scroll = new ScrollView(activity);
        scroll.addView(text);
        dialog = new AlertDialog.Builder(activity)
                .setTitle("资源错误报告")
                .setView(scroll)
                .setPositiveButton("关闭", (d, which) -> nativeAcknowledge(receipt))
                .setNeutralButton("另存为…", null)
                .setCancelable(false)
                .create();
        dialog.setOnDismissListener(d -> dialog = null);
        dialog.show();
        // Keep the report open if choosing a destination is cancelled or unavailable.
        dialog.getButton(AlertDialog.BUTTON_NEUTRAL).setOnClickListener(v -> {
            exportText = report;
            Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT)
                    .addCategory(Intent.CATEGORY_OPENABLE)
                    .setType("text/plain")
                    .putExtra(Intent.EXTRA_TITLE, "mcpe-resource-error.txt");
            try { activity.startActivityForResult(intent, SAVE_REPORT); }
            catch (android.content.ActivityNotFoundException error) {
                exportText = null;
                Toast.makeText(activity, "设备没有可用的文件保存程序", Toast.LENGTH_LONG).show();
            }
        });
    }

    public boolean onActivityResult(int request, int result, Intent data) {
        if (request != SAVE_REPORT) return false;
        String report = exportText;
        exportText = null;
        if (result != Activity.RESULT_OK || data == null || data.getData() == null || report == null) return true;
        try (OutputStream output = activity.getContentResolver().openOutputStream(data.getData(), "wt")) {
            if (output == null) throw new IOException("No output stream");
            output.write(report.getBytes(StandardCharsets.UTF_8));
            Toast.makeText(activity, "错误报告已导出", Toast.LENGTH_SHORT).show();
        } catch (IOException | SecurityException error) {
            Log.e("SunchErrors", "Cannot export report", error);
            Toast.makeText(activity, "导出失败，原始报告仍保存在应用目录", Toast.LENGTH_LONG).show();
        }
        return true;
    }

    private static native void nativeConfigure(String directory);
    private static native String nativePending();
    private static native void nativeAcknowledge(String receipt);
}
