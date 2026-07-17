package com.start.clienttest;

import android.app.Activity;
import com.irene.algui.ImguiView;

public class Main {
    public static Main instance;
    public Activity activity;

    public static void main(Activity act) {
        if (instance == null) instance = new Main();
        instance.initAndShow(act);
    }

    public void initAndShow(Activity ac) {
        this.activity = ac;
        if (!PermissionTool.isStoragePermissionGranted(activity)) {
            PermissionTool.storePermission(activity);
        }
        ImguiView.show(ac);
    }

    public int dp(int dp) {
        float scale = activity.getResources().getDisplayMetrics().density;
        return (int) (dp * scale + 0.5f);
    }


}