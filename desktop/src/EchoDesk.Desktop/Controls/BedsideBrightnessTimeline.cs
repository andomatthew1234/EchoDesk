using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Globalization;
using System.Windows;
using System.Windows.Automation.Peers;
using System.Windows.Automation;
using System.Windows.Input;
using System.Windows.Media;
using EchoDesk.Desktop.ViewModels;
using Brush = System.Windows.Media.Brush;
using Brushes = System.Windows.Media.Brushes;
using Cursors = System.Windows.Input.Cursors;
using FontFamily = System.Windows.Media.FontFamily;
using KeyEventArgs = System.Windows.Input.KeyEventArgs;
using MouseEventArgs = System.Windows.Input.MouseEventArgs;
using Pen = System.Windows.Media.Pen;
using Point = System.Windows.Point;
using Size = System.Windows.Size;

namespace EchoDesk.Desktop.Controls;

/// <summary>
/// A compact, direct-manipulation editor for the Bedside Mode 24-hour
/// brightness schedule. Exact values remain available in the adjacent form;
/// this surface is deliberately optimised for quick visual editing.
/// </summary>
public sealed class BedsideBrightnessTimeline : FrameworkElement
{
    private const double PlotLeft = 44;
    private const double PlotRight = 18;
    private const double PlotTop = 18;
    private const double PlotBottom = 34;
    private const int MinuteSnap = 15;
    private const int BrightnessSnap = 5;
    private BedsideBrightnessSegmentEditor? selected;
    private BedsideBrightnessSegmentEditor? dragging;

    public BedsideBrightnessTimeline()
    {
        Focusable = true;
        MinHeight = 190;
        Cursor = Cursors.Hand;
        SnapsToDevicePixels = true;
    }

    public static readonly DependencyProperty SegmentsProperty = DependencyProperty.Register(
        nameof(Segments), typeof(ObservableCollection<BedsideBrightnessSegmentEditor>),
        typeof(BedsideBrightnessTimeline), new FrameworkPropertyMetadata(null, OnSegmentsChanged));

    public static readonly DependencyProperty IsScheduleEnabledProperty = DependencyProperty.Register(
        nameof(IsScheduleEnabled), typeof(bool), typeof(BedsideBrightnessTimeline),
        new FrameworkPropertyMetadata(true, FrameworkPropertyMetadataOptions.AffectsRender));

    public ObservableCollection<BedsideBrightnessSegmentEditor>? Segments
    {
        get => (ObservableCollection<BedsideBrightnessSegmentEditor>?)GetValue(SegmentsProperty);
        set => SetValue(SegmentsProperty, value);
    }

    public bool IsScheduleEnabled
    {
        get => (bool)GetValue(IsScheduleEnabledProperty);
        set => SetValue(IsScheduleEnabledProperty, value);
    }

    public static int SnapMinute(double ratio) =>
        Math.Clamp((int)Math.Round(Math.Clamp(ratio, 0, 1) * 1439 / MinuteSnap) * MinuteSnap, 0, 1439);

    public static int SnapBrightness(double ratioFromTop) =>
        Math.Clamp((int)Math.Round((100 - Math.Clamp(ratioFromTop, 0, 1) * 99) / BrightnessSnap) * BrightnessSnap, 1, 100);

    protected override Size MeasureOverride(Size availableSize) =>
        new(double.IsInfinity(availableSize.Width) ? 640 : Math.Max(320, availableSize.Width), 190);

    protected override void OnRender(DrawingContext drawingContext)
    {
        base.OnRender(drawingContext);
        var foreground = BrushResource("TextBrush", Brushes.White);
        var muted = BrushResource("MutedTextBrush", Brushes.Gray);
        var border = BrushResource("BorderBrush", Brushes.DimGray);
        var surface = BrushResource("SurfaceAltBrush", Brushes.Black);
        var accent = BrushResource("AccentBrush", Brushes.White);
        var plot = PlotRect();

        drawingContext.DrawRoundedRectangle(surface, new Pen(border, 1), new Rect(0.5, 0.5, Math.Max(0, ActualWidth - 1), Math.Max(0, ActualHeight - 1)), 12, 12);
        if (plot.Width <= 0 || plot.Height <= 0) return;

        var gridPen = new Pen(border, 1);
        for (var hour = 0; hour <= 24; hour += 6)
        {
            var x = plot.Left + plot.Width * hour / 24d;
            drawingContext.DrawLine(gridPen, new Point(x, plot.Top), new Point(x, plot.Bottom));
            DrawText(drawingContext, hour == 24 ? "24" : hour.ToString("00", CultureInfo.InvariantCulture),
                muted, 11, new Point(x - (hour == 0 ? 0 : 7), plot.Bottom + 8));
        }
        foreach (var brightness in new[] { 100, 50, 1 })
        {
            var y = BrightnessY(brightness, plot);
            drawingContext.DrawLine(gridPen, new Point(plot.Left, y), new Point(plot.Right, y));
            DrawText(drawingContext, brightness == 1 ? "0" : brightness.ToString(CultureInfo.InvariantCulture), muted, 10,
                new Point(12, y - 7));
        }

        var points = OrderedPoints(plot).ToArray();
        if (points.Length > 0)
        {
            var schedulePen = new Pen(accent, 3) { LineJoin = PenLineJoin.Round };
            // The firmware treats the final point as wrapping through midnight
            // until the first point on the next day.
            drawingContext.DrawLine(schedulePen,
                new Point(plot.Left, points[^1].Point.Y), new Point(points[0].Point.X, points[^1].Point.Y));
            drawingContext.DrawLine(schedulePen,
                new Point(points[0].Point.X, points[^1].Point.Y), points[0].Point);
            for (var index = 0; index < points.Length; index++)
            {
                var current = points[index];
                var nextX = index + 1 < points.Length ? points[index + 1].Point.X : plot.Right;
                drawingContext.DrawLine(schedulePen, current.Point, new Point(nextX, current.Point.Y));
                if (index + 1 < points.Length)
                    drawingContext.DrawLine(schedulePen, new Point(nextX, current.Point.Y), points[index + 1].Point);
            }
            foreach (var point in points)
            {
                var radius = ReferenceEquals(point.Segment, selected) ? 8d : 6d;
                drawingContext.DrawEllipse(surface, new Pen(accent, ReferenceEquals(point.Segment, selected) ? 3 : 2), point.Point, radius, radius);
            }
        }

        if (!IsScheduleEnabled)
        {
            drawingContext.PushOpacity(0.72);
            drawingContext.DrawRoundedRectangle(BrushResource("SurfaceBrush", Brushes.Black), null, plot, 8, 8);
            DrawText(drawingContext, "Schedule paused — manual brightness is active", foreground, 12,
                new Point(plot.Left + 14, plot.Top + plot.Height / 2 - 9));
            drawingContext.Pop();
        }
        if (IsKeyboardFocused)
            drawingContext.DrawRoundedRectangle(null, new Pen(accent, 2), new Rect(2, 2, ActualWidth - 4, ActualHeight - 4), 11, 11);
    }

    protected override void OnMouseLeftButtonDown(MouseButtonEventArgs e)
    {
        base.OnMouseLeftButtonDown(e);
        Focus();
        if (!IsScheduleEnabled) return;
        selected = FindNearest(e.GetPosition(this), 22);
        if (selected is null) return;
        dragging = selected;
        CaptureMouse();
        UpdateDragged(e.GetPosition(this));
        e.Handled = true;
    }

    protected override void OnMouseMove(MouseEventArgs e)
    {
        base.OnMouseMove(e);
        if (dragging is null || e.LeftButton != MouseButtonState.Pressed) return;
        UpdateDragged(e.GetPosition(this));
        e.Handled = true;
    }

    protected override void OnMouseLeftButtonUp(MouseButtonEventArgs e)
    {
        base.OnMouseLeftButtonUp(e);
        if (dragging is null) return;
        UpdateDragged(e.GetPosition(this));
        dragging = null;
        ReleaseMouseCapture();
        e.Handled = true;
    }

    protected override void OnKeyDown(KeyEventArgs e)
    {
        base.OnKeyDown(e);
        var segments = Segments;
        if (!IsScheduleEnabled || segments is null || segments.Count == 0) return;
        selected ??= segments.OrderBy(segment => ParseMinute(segment.Time)).First();
        var minute = ParseMinute(selected.Time);
        switch (e.Key)
        {
            case Key.Left: selected.Time = MinuteText(FindFreeMinute(selected, Math.Max(0, minute - MinuteSnap))); break;
            case Key.Right: selected.Time = MinuteText(FindFreeMinute(selected, Math.Min(1439, minute + MinuteSnap))); break;
            case Key.Up: selected.Brightness = Math.Min(100, selected.Brightness + BrightnessSnap); break;
            case Key.Down: selected.Brightness = Math.Max(1, selected.Brightness - BrightnessSnap); break;
            case Key.PageUp: SelectAdjacent(-1); break;
            case Key.PageDown: SelectAdjacent(1); break;
            default: return;
        }
        InvalidateVisual();
        e.Handled = true;
    }

    protected override AutomationPeer OnCreateAutomationPeer() => new TimelineAutomationPeer(this);

    private static void OnSegmentsChanged(DependencyObject owner, DependencyPropertyChangedEventArgs args)
    {
        var timeline = (BedsideBrightnessTimeline)owner;
        timeline.Unsubscribe(args.OldValue as ObservableCollection<BedsideBrightnessSegmentEditor>);
        timeline.Subscribe(args.NewValue as ObservableCollection<BedsideBrightnessSegmentEditor>);
        timeline.selected = timeline.Segments?.FirstOrDefault();
        timeline.InvalidateVisual();
    }

    private void Subscribe(ObservableCollection<BedsideBrightnessSegmentEditor>? segments)
    {
        if (segments is null) return;
        segments.CollectionChanged += SegmentsCollectionChanged;
        foreach (var segment in segments) segment.PropertyChanged += SegmentPropertyChanged;
    }

    private void Unsubscribe(ObservableCollection<BedsideBrightnessSegmentEditor>? segments)
    {
        if (segments is null) return;
        segments.CollectionChanged -= SegmentsCollectionChanged;
        foreach (var segment in segments) segment.PropertyChanged -= SegmentPropertyChanged;
    }

    private void SegmentsCollectionChanged(object? sender, NotifyCollectionChangedEventArgs args)
    {
        if (args.OldItems is not null)
            foreach (BedsideBrightnessSegmentEditor segment in args.OldItems) segment.PropertyChanged -= SegmentPropertyChanged;
        if (args.NewItems is not null)
            foreach (BedsideBrightnessSegmentEditor segment in args.NewItems) segment.PropertyChanged += SegmentPropertyChanged;
        if (selected is not null && Segments?.Contains(selected) != true) selected = Segments?.FirstOrDefault();
        InvalidateVisual();
    }

    private void SegmentPropertyChanged(object? sender, PropertyChangedEventArgs args) => InvalidateVisual();

    private void UpdateDragged(Point position)
    {
        if (dragging is null) return;
        var plot = PlotRect();
        var minute = SnapMinute((position.X - plot.Left) / Math.Max(1, plot.Width));
        dragging.Time = MinuteText(FindFreeMinute(dragging, minute));
        dragging.Brightness = SnapBrightness((position.Y - plot.Top) / Math.Max(1, plot.Height));
        InvalidateVisual();
    }

    private BedsideBrightnessSegmentEditor? FindNearest(Point position, double radius)
    {
        var plot = PlotRect();
        return OrderedPoints(plot)
            .Select(item => (item.Segment, Distance: (item.Point - position).Length))
            .Where(item => item.Distance <= radius)
            .OrderBy(item => item.Distance)
            .Select(item => item.Segment)
            .FirstOrDefault();
    }

    private int FindFreeMinute(BedsideBrightnessSegmentEditor current, int desired)
    {
        var occupied = (Segments ?? [])
            .Where(segment => !ReferenceEquals(segment, current))
            .Select(segment => ParseMinute(segment.Time))
            .ToHashSet();
        if (!occupied.Contains(desired)) return desired;
        for (var offset = MinuteSnap; offset < 1440; offset += MinuteSnap)
        {
            var later = desired + offset;
            if (later <= 1439 && !occupied.Contains(later)) return later;
            var earlier = desired - offset;
            if (earlier >= 0 && !occupied.Contains(earlier)) return earlier;
        }
        return desired;
    }

    private IEnumerable<(BedsideBrightnessSegmentEditor Segment, Point Point)> OrderedPoints(Rect plot) =>
        (Segments ?? [])
        .OrderBy(segment => ParseMinute(segment.Time))
        .Select(segment => (segment, new Point(
            plot.Left + plot.Width * ParseMinute(segment.Time) / 1439d,
            BrightnessY(segment.Brightness, plot))));

    private void SelectAdjacent(int direction)
    {
        var ordered = (Segments ?? []).OrderBy(segment => ParseMinute(segment.Time)).ToArray();
        if (ordered.Length == 0) return;
        var index = Math.Max(0, Array.IndexOf(ordered, selected));
        selected = ordered[(index + direction + ordered.Length) % ordered.Length];
    }

    private Rect PlotRect() => new(PlotLeft, PlotTop,
        Math.Max(0, ActualWidth - PlotLeft - PlotRight), Math.Max(0, ActualHeight - PlotTop - PlotBottom));

    private static double BrightnessY(int brightness, Rect plot) =>
        plot.Top + plot.Height * (100 - Math.Clamp(brightness, 1, 100)) / 99d;

    private Brush BrushResource(string key, Brush fallback) => TryFindResource(key) as Brush ?? fallback;

    private void DrawText(DrawingContext context, string text, Brush brush, double size, Point origin)
    {
        var formatted = new FormattedText(text, CultureInfo.CurrentUICulture, System.Windows.FlowDirection.LeftToRight,
            new Typeface(new FontFamily("Segoe UI"), FontStyles.Normal, FontWeights.Normal, FontStretches.Normal), size, brush,
            VisualTreeHelper.GetDpi(this).PixelsPerDip);
        context.DrawText(formatted, origin);
    }

    private static int ParseMinute(string? value) =>
        TimeSpan.TryParseExact(value?.Trim(), "hh\\:mm", CultureInfo.InvariantCulture, out var time)
            && time >= TimeSpan.Zero && time < TimeSpan.FromDays(1)
            ? (int)time.TotalMinutes : 0;

    private static string MinuteText(int minute) =>
        TimeSpan.FromMinutes(Math.Clamp(minute, 0, 1439)).ToString(@"hh\:mm", CultureInfo.InvariantCulture);

    private sealed class TimelineAutomationPeer(BedsideBrightnessTimeline owner) : FrameworkElementAutomationPeer(owner)
    {
        protected override string GetClassNameCore() => nameof(BedsideBrightnessTimeline);
        protected override string GetNameCore() =>
            AutomationProperties.GetName(Owner) is { Length: > 0 } name ? name : "Bedside brightness schedule";
        protected override AutomationControlType GetAutomationControlTypeCore() => AutomationControlType.Custom;
    }
}
