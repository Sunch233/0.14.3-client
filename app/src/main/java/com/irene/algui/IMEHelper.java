package com.irene.algui;

import android.app.Activity;
import android.content.Context;
import android.graphics.Color;
import android.graphics.PixelFormat;
import android.graphics.drawable.GradientDrawable;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.InputType;
import android.util.Log;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.WindowManager;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.PopupWindow;
import java.lang.ref.WeakReference;
public class IMEHelper {

    private static final String TAG = "IMEHelper";
    private static WeakReference<Activity> activityRef;
    private static InputMethodManager imm;
    private static InputMethodView inputView;
    private static WindowManager windowManager;
    private static volatile boolean isInputActive = false;
    private static volatile String currentText = "";
    private static InputTextCallback inputCallback;
    private static final Handler mainHandler = new Handler(Looper.getMainLooper());
    private static android.widget.ListPopupWindow listPopupWindow;
    private static PopupWindow horizontalPopupWindow;
    private static View anchorView;

    public interface InputTextCallback {
        void onInputTextChanged(String text);
    }

    public static void init(Activity activity) {
        if (activity == null) return;
        activityRef = new WeakReference<>(activity);
        imm = (InputMethodManager) activity.getSystemService(Context.INPUT_METHOD_SERVICE);
        windowManager = (WindowManager) activity.getSystemService(Context.WINDOW_SERVICE);

        inputView = new InputMethodView(activity);
        anchorView = new View(activity);
        Log.i(TAG, "IMEHelper initialized");
    }

    public static void showKeyboard(String initialText) {
        runOnMain(() -> {
            Activity activity = activityRef != null ? activityRef.get() : null;
            if (activity == null) {
                Log.e(TAG, "showKeyboard failed: activity is null");
                return;
            }
            if (inputView == null) {
                Log.e(TAG, "showKeyboard failed: inputView is null");
                return;
            }
            if (imm == null) {
                Log.e(TAG, "showKeyboard failed: imm is null");
                return;
            }

            currentText = initialText != null ? initialText : "";
            isInputActive = true;

            inputView.setText(currentText);
            inputView.setFocusable(true);
            inputView.setFocusableInTouchMode(true);

            try {
                if (inputView.getParent() == null) {
                    WindowManager.LayoutParams params = new WindowManager.LayoutParams();
                    params.width = 1;
                    params.height = 1;
                    params.type = WindowManager.LayoutParams.TYPE_APPLICATION;
                    params.gravity = Gravity.START | Gravity.TOP;
                    params.x = 0;
                    params.y = 0;
                    params.flags = WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE
                            | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN
                            | WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED;
                    params.format = PixelFormat.TRANSPARENT;

                    windowManager.addView(inputView, params);
                    Log.d(TAG, "inputView added to window");
                }
            } catch (Exception e) {
                Log.e(TAG, "Failed to add inputView to window", e);
            }

            boolean focusResult = inputView.requestFocus();
            Log.d(TAG, "requestFocus result: " + focusResult);

            inputView.postDelayed(() -> {
                try {
                    boolean result = imm.showSoftInput(inputView, InputMethodManager.SHOW_FORCED);
                    //Log.d(TAG, "showSoftInput result: " + result + ", text='" + currentText + "', isActive=" + isInputActive);

                    if (!result) {
                        Log.w(TAG, "showSoftInput returned false, trying toggleSoftInput");
                        imm.toggleSoftInput(InputMethodManager.SHOW_FORCED, 0);
                    }
                } catch (Exception e) {
                    Log.e(TAG, "showSoftInput exception", e);
                }
            }, 200);
        });
    }

    public static void hideKeyboard() {
        runOnMain(() -> {
            dismissPopupMenu();
            if (inputView != null && imm != null) {
                try {
                    imm.hideSoftInputFromWindow(inputView.getWindowToken(), 0);
                } catch (Exception e) {
                    Log.e(TAG, "hideSoftInput exception", e);
                }
                inputView.clearFocus();

                if (inputView.getParent() != null && windowManager != null) {
                    try {
                        windowManager.removeView(inputView);
                        Log.d(TAG, "inputView removed from window");
                    } catch (Exception e) {
                        Log.e(TAG, "removeView exception", e);
                    }
                }
            }
            isInputActive = false;
            Log.d(TAG, "hideKeyboard");
        });
    }

    public static void showPopupMenu(float x, float y) {
        runOnMain(() -> {
            Activity activity = activityRef != null ? activityRef.get() : null;
            if (activity == null || currentText.isEmpty()) return;

            dismissPopupMenu();

            try {
                WindowManager.LayoutParams params = new WindowManager.LayoutParams();
                params.width = 1;
                params.height = 1;
                params.type = WindowManager.LayoutParams.TYPE_APPLICATION;
                params.gravity = Gravity.START | Gravity.TOP;
                params.x = (int) x;
                params.y = (int) y;
                params.flags = WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE
                        | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN;
                params.format = PixelFormat.TRANSPARENT;

                windowManager.addView(anchorView, params);

                anchorView.post(() -> {
                    int itemWidth = dipToPx(activity, 60);
                    int itemHeight = dipToPx(activity, 36);
                    int padding = dipToPx(activity, 2);
                    int cornerRadius = dipToPx(activity, 8);

                    LinearLayout layout = new LinearLayout(activity);
                    layout.setOrientation(LinearLayout.HORIZONTAL);
                    layout.setBackgroundColor(Color.TRANSPARENT);
                    layout.setPadding(padding, padding, padding, padding);

                    GradientDrawable background = new GradientDrawable();
                    background.setColor(Color.parseColor("#1a1a1a"));
                    background.setCornerRadius(cornerRadius);
                    layout.setBackground(background);
                    //TODO全选时imgui输入框也全选(也许要实现?
                    String[] items = {"复制", "全选", "剪切", "粘贴"};

                    for (int i = 0; i < items.length; i++) {
                        final int position = i;
                        Button button = new Button(activity);
                        button.setText(items[i]);
                        button.setTextColor(Color.WHITE);
                        button.setBackgroundColor(Color.TRANSPARENT);
                        button.setTextSize(12);
                        button.setPadding(0, 0, 0, 0);

                        LinearLayout.LayoutParams buttonParams = new LinearLayout.LayoutParams(
                                itemWidth,
                                itemHeight
                        );
                        if (i > 0) {
                            buttonParams.leftMargin = padding;
                        }
                        button.setLayoutParams(buttonParams);

                        button.setOnClickListener(v -> {
                            dismissPopupMenu();
                            switch (position) {
                                case 0:
                                    copyToClipboard(activity);
                                    break;
                                case 1:
                                    break;
                                case 2:
                                    cutToClipboard(activity);
                                    break;
                                case 3:
                                    pasteFromClipboard(activity);
                                    break;
                            }
                        });

                        layout.addView(button);
                    }

                    int totalWidth = itemWidth * items.length + padding * (items.length + 1);
                    int totalHeight = itemHeight + padding * 2;

                    horizontalPopupWindow = new PopupWindow(layout, totalWidth, totalHeight, true);
                    horizontalPopupWindow.setBackgroundDrawable(new android.graphics.drawable.ColorDrawable(Color.TRANSPARENT));
                    horizontalPopupWindow.setOutsideTouchable(true);

                    horizontalPopupWindow.setOnDismissListener(() -> {
                        if (anchorView.getParent() != null && windowManager != null) {
                            try {
                                windowManager.removeView(anchorView);
                            } catch (Exception e) {
                                Log.e(TAG, "removeView exception", e);
                            }
                        }
                    });

                    int offsetX = -totalWidth / 2;
                    int offsetY = -totalHeight - dipToPx(activity, 10);

                    horizontalPopupWindow.showAsDropDown(anchorView, offsetX, offsetY);
                });

            } catch (Exception e) {
                Log.e(TAG, "Failed to show popup menu", e);
                e.printStackTrace();
                if (anchorView.getParent() != null && windowManager != null) {
                    try {
                        windowManager.removeView(anchorView);
                    } catch (Exception ex) {
                        Log.e(TAG, "cleanup exception", ex);
                    }
                }
            }
        });
    }

    private static int dipToPx(Context context, float dip) {
        float density = context.getResources().getDisplayMetrics().density;
        return (int) (dip * density + 0.5f);
    }

    public static void dismissPopupMenu() {
        if (listPopupWindow != null) {
            try {
                listPopupWindow.dismiss();
            } catch (Exception e) {
                Log.e(TAG, "dismiss exception", e);
            }
            listPopupWindow = null;
        }
        if (horizontalPopupWindow != null) {
            try {
                horizontalPopupWindow.dismiss();
            } catch (Exception e) {
                Log.e(TAG, "dismiss exception", e);
            }
            horizontalPopupWindow = null;
        }
        if (anchorView != null && anchorView.getParent() != null && windowManager != null) {
            try {
                windowManager.removeView(anchorView);
            } catch (Exception e) {
                Log.e(TAG, "removeView exception", e);
            }
        }
    }

    private static void copyToClipboard(Context context) {
        android.content.ClipboardManager clipboard =
                (android.content.ClipboardManager) context.getSystemService(Context.CLIPBOARD_SERVICE);
        android.content.ClipData clip = android.content.ClipData.newPlainText("text", currentText);
        clipboard.setPrimaryClip(clip);
    }

    private static void cutToClipboard(Context context) {
        copyToClipboard(context);
        currentText = "";
        if (inputCallback != null) {
            inputCallback.onInputTextChanged(currentText);
        }
    }

    private static void pasteFromClipboard(Context context) {
        android.content.ClipboardManager clipboard =
                (android.content.ClipboardManager) context.getSystemService(Context.CLIPBOARD_SERVICE);
        if (clipboard != null && clipboard.hasPrimaryClip()) {
            android.content.ClipData clip = clipboard.getPrimaryClip();
            if (clip != null && clip.getItemCount() > 0) {
                CharSequence pasteText = clip.getItemAt(0).getText();
                if (pasteText != null) {
                    currentText = currentText + pasteText.toString();
                    if (inputCallback != null) {
                        inputCallback.onInputTextChanged(currentText);
                    }
                }
            }
        }
    }

    public static String getInputText() {
        return currentText;
    }

    public static boolean isKeyboardVisible() {
        return isInputActive;
    }

    public static void setInputCallback(InputTextCallback callback) {
        inputCallback = callback;
    }

    private static void runOnMain(Runnable action) {
        if (Looper.getMainLooper() == Looper.myLooper()) {
            action.run();
        } else {
            mainHandler.post(action);
        }
    }

    static class InputMethodView extends View {
        private String text = "";

        public InputMethodView(Context context) {
            super(context);
            setFocusable(true);
            setFocusableInTouchMode(true);
        }

        public void setText(String text) {
            this.text = text != null ? text : "";
        }

        @Override
        public boolean onCheckIsTextEditor() {
            return true;
        }

        @Override
        public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
            outAttrs.imeOptions = EditorInfo.IME_ACTION_DONE | EditorInfo.IME_FLAG_NO_FULLSCREEN;
            outAttrs.inputType = InputType.TYPE_CLASS_TEXT;
            return new BaseInputConnection(this, true) {
                @Override
                public Editable getEditable() {
                    return new android.text.SpannableStringBuilder(currentText);
                }

                @Override
                public boolean commitText(CharSequence text, int newCursorPosition) {
                    String newText = text.toString();
                    currentText = currentText + newText;
                    if (inputCallback != null) {
                        inputCallback.onInputTextChanged(currentText);
                    }
                    return true;
                }

                @Override
                public boolean deleteSurroundingText(int beforeLength, int afterLength) {
                    boolean deleted = false;
                    if (beforeLength > 0 && currentText.length() >= beforeLength) {
                        int startPos = currentText.length() - beforeLength;
                        currentText = currentText.substring(0, startPos);
                        deleted = true;
                    }

                    if (afterLength > 0 && currentText.length() > afterLength) {
                        currentText = currentText.substring(0, currentText.length() - afterLength);
                        deleted = true;
                    }

                    if (deleted && inputCallback != null) {
                        inputCallback.onInputTextChanged(currentText);
                    }

                    return true;
                }

                @Override
                public boolean sendKeyEvent(KeyEvent event) {
                    if (event.getAction() == KeyEvent.ACTION_DOWN) {
                        if (event.getKeyCode() == KeyEvent.KEYCODE_DEL) {
                            //Log.d(TAG, "KEYCODE_DEL pressed, current='" + currentText + "'");
                            if (!currentText.isEmpty()) {
                                currentText = currentText.substring(0, currentText.length() - 1);
                                if (inputCallback != null) {
                                    inputCallback.onInputTextChanged(currentText);
                                }
                                //Log.d(TAG, "After DEL: '" + currentText + "'");
                            }
                            return true;
                        } else if (event.getKeyCode() == KeyEvent.KEYCODE_FORWARD_DEL) {
                            //Log.d(TAG, "KEYCODE_FORWARD_DEL pressed");
                            return true;
                        }
                    }
                    return super.sendKeyEvent(event);
                }

                @Override
                public boolean finishComposingText() {
                    return true;
                }

                @Override
                public boolean setComposingText(CharSequence text, int newCursorPosition) {
                    return super.setComposingText(text, newCursorPosition);
                }
            };
        }
    }
}