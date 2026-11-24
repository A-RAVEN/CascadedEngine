// editor-web-render-spike 2.3 / 3.1：渲染原型的同源对账与读数。
//
// 数据流（design D3 / D4）：
//   C++ 读回共享纹理（Readback 绑定面 CA_BIND_SKIP，读像素只在 C++）
//     → C++ 把 RGBA8 字节 + 整图哈希 + 多点采样打包成 `spike-frame` 经 EditorService WebSocket 发进来
//     → 本类转发给原型页（同一条 WebSocket 的另一个客户端）
//     → 原型页 putImageData 上屏后 getImageData 回读，把 帧序号 / 整图哈希 / 七点采样 / canvas 尺寸·位置·
//       可见性 包成 `spike-report` 发回来
//     → 本类比对**同一帧**的两份数据：不一致记 E_WEBCANVAS_MISMATCH，显示类读数不合法记
//       E_WEBCANVAS_DISPLAY，6000ms 没回传记 E_WEBCANVAS_NOLINE。
//
// 线程：所有方法只在引擎主线程（DrainInbound / Tick）调用；解析在监听线程，语义比较与状态在主线程。
// 首错优先：一旦记了错误码就不再改（红态读数不会因为后续帧变成另一个原因）。
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace Scripting.Managed.Editor
{
    internal static class SpikeChannel
    {
        // 特征图案固定尺寸（D2 写死 256×256；与 C++ 的 kTextureSize、网页 canvas 的 width/height 同值）。
        internal const int TextureSize = 256;

        // 回传超时 6000 毫秒：与既有像素等待先例同值（Test/GPUBackendTester/Main.cpp 的 6000ms 帧等待）。
        internal const int ReportTimeoutMilliseconds = 6000;

        internal const string ErrorMismatch = "E_WEBCANVAS_MISMATCH";
        internal const string ErrorDisplay = "E_WEBCANVAS_DISPLAY";
        internal const string ErrorNoLine = "E_WEBCANVAS_NOLINE";

        private static bool s_pageReady;
        private static int s_pageWebgpu;
        private static int s_framesRelayed;
        private static int s_matched;
        private static string s_path = "-";
        private static string s_error = string.Empty;
        private static string s_errorDetail = "-";

        private static bool s_pending;
        private static int s_pendingFrame = -1;
        private static string s_pendingHash = string.Empty;
        private static List<SpikeSampleValue> s_pendingSamples;
        private static DateTime s_pendingDeadlineUtc;

        internal static bool PageReady => s_pageReady;
        internal static string ErrorCode => s_error;
        internal static string ErrorDetail => s_errorDetail;
        internal static int Matched => s_matched;
        /// <summary>已转发给原型页的帧数（抓窗钩子据此判断"帧到了，该补抓一张"）。</summary>
        internal static int FramesRelayed => s_framesRelayed;
        /// <summary>实际走的路径（来自最近一帧 spike-frame 的 path 字段；本批固定 cpu-readback）。</summary>
        internal static string Path => s_path;

        /// <summary>服务启动时清一次（同一进程不会重复跑原型用例，但不留脏状态）。</summary>
        internal static void Reset()
        {
            s_pageReady = false;
            s_pageWebgpu = 0;
            s_framesRelayed = 0;
            s_matched = 0;
            s_path = "-";
            s_error = string.Empty;
            s_errorDetail = "-";
            s_pending = false;
            s_pendingFrame = -1;
            s_pendingHash = string.Empty;
            s_pendingSamples = null;
            s_pendingDeadlineUtc = DateTime.MinValue;
        }

        /// <summary>原型页就绪（收到 spike-hello）：把 spikePage=1 暴露到读数行。</summary>
        internal static void AcceptHello(int webgpu)
        {
            s_pageReady = true;
            s_pageWebgpu = webgpu;
            EditorService.LogServiceMarker("[渲染原型 读数] spikeHello webgpu=" + webgpu +
                                           "（原型页已握手，可以开始推帧）");
        }

        /// <summary>收到一帧原生读回：登记"这一帧的原生哈希 + 采样 + 6000ms 回传截止时间"。</summary>
        internal static void AcceptFrame(SpikeFramePayload frame)
        {
            if (s_error.Length != 0)
            {
                return; // 已经判红：后续帧不再刷新读数（首错优先）
            }
            ++s_framesRelayed;
            s_path = frame.Path.Length == 0 ? "-" : frame.Path;
            s_pending = true;
            s_pendingFrame = frame.Frame;
            s_pendingHash = frame.Hash;
            s_pendingSamples = frame.Samples;
            s_pendingDeadlineUtc = DateTime.UtcNow.AddMilliseconds(ReportTimeoutMilliseconds);
        }

        /// <summary>收到原型页的对账数据：同帧比对（像素类先判，显示类后判）。</summary>
        internal static void AcceptReport(SpikeReportPayload report)
        {
            if (s_error.Length != 0)
            {
                return;
            }
            if (!s_pending)
            {
                // 没有待对账的帧：说明回传比帧先到（或重复回传），按"缺同帧"处理会让红态误报成超时，
                // 所以只记一行、不判红。
                EditorService.LogServiceMarker("[渲染原型 读数] spikeReportIgnored frame=" + report.Frame +
                                               "（没有待对账的原生帧）");
                return;
            }
            if (report.Frame != s_pendingFrame)
            {
                RecordError(ErrorMismatch, "回传帧号与待对账帧号不一致：原生 " + s_pendingFrame + " / 网页 " +
                                           report.Frame);
                return;
            }
            string mismatch = DescribeMismatch(report);
            if (mismatch.Length != 0)
            {
                RecordError(ErrorMismatch, mismatch);
                return;
            }
            string displayProblem = DescribeDisplayProblem(report);
            if (displayProblem.Length != 0)
            {
                RecordError(ErrorDisplay, displayProblem);
                return;
            }
            s_pending = false;
            ++s_matched;
            EditorService.LogServiceMarker("[渲染原型 读数] spikeMatch frame=" + report.Frame + " hash=" +
                                           report.Hash + " canvas=" + report.CanvasWidth + "x" +
                                           report.CanvasHeight + " path=" + s_path + " webgpu=" + report.Webgpu);
        }

        /// <summary>每帧调用：超过 6000ms 没收到同帧回传 → 断链/缺回传判红。</summary>
        internal static void TickTimeout()
        {
            if (s_error.Length != 0 || !s_pending)
            {
                return;
            }
            if (DateTime.UtcNow < s_pendingDeadlineUtc)
            {
                return;
            }
            RecordError(ErrorNoLine, "帧 " + s_pendingFrame + " 发出后 " + ReportTimeoutMilliseconds +
                                     "ms 内没有收到同帧回传（原型页断开？spike-report 未发出？）");
            s_pending = false;
        }

        /// <summary>把原型通道的读数追加到 `[编辑器 读数]` 行（只在 spike 模式调用）。</summary>
        internal static void AppendReadings(StringBuilder builder)
        {
            builder.Append(" spike=1");
            builder.Append(" spikePage=").Append(s_pageReady ? "1" : "0");
            builder.Append(" spikeWebgpu=").Append(s_pageWebgpu.ToString(CultureInfo.InvariantCulture));
            builder.Append(" spikeFrames=").Append(s_framesRelayed.ToString(CultureInfo.InvariantCulture));
            builder.Append(" spikeMatched=").Append(s_matched.ToString(CultureInfo.InvariantCulture));
            builder.Append(" spikePath=").Append(EditorService.Sanitize(s_path));
            builder.Append(" spikeError=").Append(s_error.Length == 0 ? "-" : EditorService.Sanitize(s_error));
        }

        private static void RecordError(string code, string detail)
        {
            if (s_error.Length != 0)
            {
                return;
            }
            s_error = code;
            s_errorDetail = detail;
            EditorService.LogServiceMarker("[渲染原型 读数] spikeError=" + code + " detail=" +
                                           EditorService.Sanitize(detail));
        }

        /// <summary>像素类同源对账：帧序号（外层已判）、尺寸回显、哈希算法、整图哈希、七点采样。</summary>
        private static string DescribeMismatch(SpikeReportPayload report)
        {
            if (report.Width != TextureSize || report.Height != TextureSize)
            {
                return "网页回传的帧尺寸不是 " + TextureSize + "×" + TextureSize + "：" + report.Width + "×" +
                       report.Height;
            }
            if (!string.Equals(report.HashAlgorithm, "fnv1a32", StringComparison.Ordinal))
            {
                return "哈希算法不是 fnv1a32：" + report.HashAlgorithm;
            }
            if (!string.Equals(report.Hash, s_pendingHash, StringComparison.Ordinal))
            {
                return "整图哈希不一致：原生 " + s_pendingHash + " / 网页 " + report.Hash;
            }
            if (s_pendingSamples == null || s_pendingSamples.Count == 0)
            {
                return "原生帧没有采样点（spike-frame 的 samples 为空）";
            }
            foreach (SpikeSampleValue expected in s_pendingSamples)
            {
                SpikeSampleValue actual = null;
                foreach (SpikeSampleValue candidate in report.Samples)
                {
                    if (string.Equals(candidate.Name, expected.Name, StringComparison.Ordinal))
                    {
                        actual = candidate;
                        break;
                    }
                }
                if (actual == null)
                {
                    return "网页缺采样点 " + expected.Name;
                }
                if (actual.R != expected.R || actual.G != expected.G || actual.B != expected.B ||
                    actual.A != expected.A)
                {
                    return "采样点 " + expected.Name + " 不一致：原生 (" + expected.R + "," + expected.G + "," +
                           expected.B + "," + expected.A + ") / 网页 (" + actual.R + "," + actual.G + "," +
                           actual.B + "," + actual.A + ")";
                }
            }
            return string.Empty;
        }

        /// <summary>显示类读数：canvas 像素尺寸 / 布局尺寸 / display / visibility（隐藏画布靠这组判红）。</summary>
        private static string DescribeDisplayProblem(SpikeReportPayload report)
        {
            if (report.CanvasWidth != TextureSize || report.CanvasHeight != TextureSize)
            {
                return "canvas 像素尺寸不是 " + TextureSize + "×" + TextureSize + "：" + report.CanvasWidth + "×" +
                       report.CanvasHeight;
            }
            if (report.RectWidth <= 0.0 || report.RectHeight <= 0.0)
            {
                return "canvas 布局尺寸为 0：rect=" + report.RectWidth.ToString(CultureInfo.InvariantCulture) + "×" +
                       report.RectHeight.ToString(CultureInfo.InvariantCulture);
            }
            if (string.Equals(report.Display, "none", StringComparison.Ordinal))
            {
                return "canvas 的 computed display=none（画布被隐藏）";
            }
            if (!string.Equals(report.Visibility, "visible", StringComparison.Ordinal))
            {
                return "页面 visibilityState 不是 visible：" + report.Visibility;
            }
            return string.Empty;
        }
    }
}
