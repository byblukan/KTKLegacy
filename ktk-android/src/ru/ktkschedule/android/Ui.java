package ru.ktkschedule.android;

import android.app.AlertDialog;
import android.content.Context;
import android.content.res.TypedArray;
import android.graphics.drawable.Drawable;
import android.util.TypedValue;

final class Ui {
    private Ui() {
    }

    static void alert(Context context, String message) {
        new AlertDialog.Builder(context).setMessage(message).setPositiveButton(android.R.string.ok, null).show();
    }

    static int badgeColor(String badge) {
        if (badge.startsWith("5")) {
            return 0xFF298C33;
        }
        if (badge.startsWith("4")) {
            return 0xFF2666BF;
        }
        if (badge.startsWith("3")) {
            return 0xFFD98014;
        }
        if (badge.startsWith("2") || badge.startsWith("1") || badge.equals("Н")) {
            return 0xFFCC2626;
        }
        return 0xFF555555;
    }

    static int dp(Context context, float value) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value, context.getResources().getDisplayMetrics());
    }

    /** The theme's pressed/focused highlight for a clickable row. */
    static Drawable selectableBackground(Context context) {
        TypedArray a = context.obtainStyledAttributes(new int[]{android.R.attr.selectableItemBackground});
        try {
            return a.getDrawable(0);
        } finally {
            a.recycle();
        }
    }
}
