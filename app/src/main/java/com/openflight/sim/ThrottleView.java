package com.openflight.sim;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.View;

/**
 * Вертикальный слайдер тяги (РУД): 0% внизу, 100% вверху.
 */
public class ThrottleView extends View {

    public interface Listener {
        void onChanged(float value); // 0..1
    }

    private final Paint trackPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint fillPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint knobPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF track = new RectF();
    private Listener listener;
    private float value = 0f;

    public ThrottleView(Context context, AttributeSet attrs) {
        super(context, attrs);
        trackPaint.setColor(Color.argb(70, 0, 0, 0));
        trackPaint.setStyle(Paint.Style.STROKE);
        trackPaint.setStrokeWidth(3f);
        fillPaint.setColor(Color.argb(140, 90, 200, 90));
        knobPaint.setColor(Color.argb(220, 255, 255, 255));
        textPaint.setColor(Color.WHITE);
        textPaint.setTextSize(26f);
        textPaint.setTextAlign(Paint.Align.CENTER);
    }

    public void setListener(Listener l) {
        listener = l;
    }

    private void updateFromTouch(MotionEvent event) {
        float h = getHeight();
        float pad = 40f;
        float usable = h - pad * 2;
        float v = 1f - (event.getY() - pad) / usable;
        value = Math.max(0f, Math.min(1f, v));
        if (listener != null) listener.onChanged(value);
        invalidate();
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_MOVE:
                updateFromTouch(event);
                return true;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                return true;
        }
        return true;
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        float w = getWidth(), h = getHeight();
        float pad = 40f;
        track.set(w * 0.30f, pad, w * 0.70f, h - pad);
        canvas.drawRoundRect(track, 14f, 14f, trackPaint);

        float usable = track.height();
        float fillH = usable * value;
        RectF fill = new RectF(track.left, track.bottom - fillH, track.right, track.bottom);
        canvas.drawRoundRect(fill, 14f, 14f, fillPaint);

        float knobY = track.bottom - usable * value;
        canvas.drawCircle(w / 2f, knobY, 22f, knobPaint);

        canvas.drawText((int) (value * 100) + "%", w / 2f, pad - 10f, textPaint);
        canvas.drawText("ТЯГА", w / 2f, h - pad + 28f, textPaint);
    }
}
