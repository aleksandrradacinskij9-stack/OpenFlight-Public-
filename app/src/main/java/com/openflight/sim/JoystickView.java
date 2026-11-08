package com.openflight.sim;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.View;

/**
 * Виртуальный джойстик: крен (влево/вправо) и тангаж
 * (тянем палец на себя/вниз = штурвал на себя = подъём носа).
 */
public class JoystickView extends View {

    public interface Listener {
        void onChanged(float x, float y); // x, y в диапазоне -1..1
    }

    private final Paint bgPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint ringPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint knobPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint crossPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private Listener listener;
    private float knobX = 0f, knobY = 0f;

    public JoystickView(Context context, AttributeSet attrs) {
        super(context, attrs);
        bgPaint.setColor(Color.argb(70, 0, 0, 0));
        ringPaint.setColor(Color.argb(150, 255, 255, 255));
        ringPaint.setStyle(Paint.Style.STROKE);
        ringPaint.setStrokeWidth(3f);
        knobPaint.setColor(Color.argb(200, 255, 255, 255));
        crossPaint.setColor(Color.argb(90, 255, 255, 255));
        crossPaint.setStrokeWidth(2f);
    }

    public void setListener(Listener l) {
        listener = l;
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        float cx = getWidth() / 2f, cy = getHeight() / 2f;
        float r = Math.min(cx, cy) - 6f;

        canvas.drawCircle(cx, cy, r, bgPaint);
        canvas.drawCircle(cx, cy, r, ringPaint);
        canvas.drawLine(cx - r * 0.7f, cy, cx + r * 0.7f, cy, crossPaint);
        canvas.drawLine(cx, cy - r * 0.7f, cx, cy + r * 0.7f, crossPaint);
        canvas.drawCircle(cx + knobX * r * 0.75f, cy + knobY * r * 0.75f, r * 0.28f, knobPaint);
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        float cx = getWidth() / 2f, cy = getHeight() / 2f;
        float r = Math.min(cx, cy) - 6f;
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_MOVE: {
                float dx = (event.getX() - cx) / (r * 0.75f);
                float dy = (event.getY() - cy) / (r * 0.75f);
                float len = (float) Math.sqrt(dx * dx + dy * dy);
                if (len > 1f) { dx /= len; dy /= len; }
                knobX = dx;
                knobY = dy;
                if (listener != null) listener.onChanged(dx, dy);
                invalidate();
                return true;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                knobX = 0f;
                knobY = 0f;
                if (listener != null) listener.onChanged(0f, 0f);
                invalidate();
                return true;
        }
        return true;
    }
}
