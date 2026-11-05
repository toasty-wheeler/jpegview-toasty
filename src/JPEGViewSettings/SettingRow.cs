using System;
using System.Drawing;
using System.Globalization;
using System.Linq;
using System.Text.RegularExpressions;
using System.Windows.Forms;

namespace JPEGViewSettings
{
    // One setting: title, key, documentation on the left, editor on the right
    public class SettingRow : Panel
    {
        public Setting Setting { get; }
        public bool ResetRequested { get; private set; }
        public event EventHandler ValueChanged;

        readonly Func<int, int> S;
        readonly bool m_canDeleteKey;
        readonly Label m_title, m_key, m_description, m_perf;
        readonly LinkLabel m_reset;
        readonly Control m_editor;
        readonly Button m_pickButton;
        bool m_suppress;

        static readonly Color AccentColor = Color.FromArgb(0, 95, 184);
        static readonly Color DimColor = Color.FromArgb(96, 96, 96);
        static readonly Color PerfColor = Color.FromArgb(176, 96, 0);

        public SettingRow(Setting setting, bool canDeleteKey, Font font, Func<int, int> scale)
        {
            Setting = setting;
            S = scale;
            m_canDeleteKey = canDeleteKey;
            BackColor = Color.White;
            Margin = new Padding(0, 0, 0, 1);
            Font = font;

            m_title = new Label { AutoSize = true, Font = new Font(font, FontStyle.Bold), Text = Humanize(setting.Key) };
            m_key = new Label { AutoSize = true, ForeColor = DimColor, Text = setting.Key };
            m_description = new Label { AutoSize = true, ForeColor = Color.FromArgb(48, 48, 48), Text = setting.Description };
            m_perf = new Label { AutoSize = true, ForeColor = PerfColor, Text = setting.PerformanceHint == null ? "" : "⚡ " + setting.PerformanceHint, Visible = setting.PerformanceHint != null };
            m_reset = new LinkLabel { AutoSize = true, Text = "Reset to default" };
            m_reset.LinkClicked += (s, e) => RequestReset();
            Controls.AddRange(new Control[] { m_title, m_key, m_description, m_perf, m_reset });

            switch (setting.Kind)
            {
                case SettingKind.Bool:
                    var check = new CheckBox { AutoSize = true, Text = "Enabled" };
                    check.CheckedChanged += (s, e) => OnEditorChanged();
                    m_editor = check;
                    break;
                case SettingKind.Choice:
                    var combo = new ComboBox { DropDownStyle = setting.ChoiceEditable ? ComboBoxStyle.DropDown : ComboBoxStyle.DropDownList };
                    combo.Items.AddRange(setting.Choices.Select(c => c.Length == 0 ? "(same as windowed)" : c).ToArray());
                    combo.TextChanged += (s, e) => OnEditorChanged();
                    combo.SelectedIndexChanged += (s, e) => OnEditorChanged();
                    m_editor = combo;
                    break;
                default:
                    var text = new TextBox();
                    text.TextChanged += (s, e) => OnEditorChanged();
                    m_editor = text;
                    break;
            }
            Controls.Add(m_editor);

            if (setting.Kind == SettingKind.Color || setting.Kind == SettingKind.Font)
            {
                m_pickButton = new Button { Text = setting.Kind == SettingKind.Color ? "" : "Choose...", FlatStyle = FlatStyle.Flat };
                m_pickButton.Click += (s, e) => Pick();
                Controls.Add(m_pickButton);
            }

            SetEditorValue(setting.EffectiveValue);
            UpdateState();
        }

        public string CurrentValue
        {
            get
            {
                switch (m_editor)
                {
                    case CheckBox c: return c.Checked ? "true" : "false";
                    case ComboBox c:
                        string t = c.DropDownStyle == ComboBoxStyle.DropDownList ? (c.SelectedItem as string ?? "") : c.Text;
                        return t == "(same as windowed)" ? "" : t;
                    default: return m_editor.Text;
                }
            }
        }

        public bool IsDirty => ResetRequested || !SameValue(CurrentValue, Setting.EffectiveValue);

        public bool IsChangedFromDefault => Setting.BaselineValue != null && !SameValue(CurrentValue, Setting.BaselineValue);

        public bool IsValid => !Setting.IsNumeric || CurrentValue.Trim().Length == 0 || IniModel.IsNumber(CurrentValue);

        public bool Matches(string filter) =>
            filter.Length == 0 ||
            Setting.Key.IndexOf(filter, StringComparison.OrdinalIgnoreCase) >= 0 ||
            m_title.Text.IndexOf(filter, StringComparison.OrdinalIgnoreCase) >= 0 ||
            Setting.Description.IndexOf(filter, StringComparison.OrdinalIgnoreCase) >= 0;

        // Called after saving: the current value is now the stored value
        public void AcceptSaved()
        {
            ResetRequested = false;
            UpdateState();
        }

        static bool SameValue(string a, string b)
        {
            a = (a ?? "").Trim();
            b = (b ?? "").Trim();
            // "0.00" and "0.0" are the same value for JPEGView
            if (double.TryParse(a, NumberStyles.Float, CultureInfo.InvariantCulture, out double da) &&
                double.TryParse(b, NumberStyles.Float, CultureInfo.InvariantCulture, out double db))
                return da == db;
            return string.Equals(a, b, StringComparison.OrdinalIgnoreCase);
        }

        void SetEditorValue(string value)
        {
            m_suppress = true;
            switch (m_editor)
            {
                case CheckBox c:
                    c.Checked = value.Trim().Equals("true", StringComparison.OrdinalIgnoreCase);
                    break;
                case ComboBox c:
                    string display = value.Length == 0 && Setting.Choices.Contains("") ? "(same as windowed)" : value;
                    int index = -1;
                    for (int i = 0; i < c.Items.Count; i++)
                        if (string.Equals((string)c.Items[i], display, StringComparison.OrdinalIgnoreCase)) { index = i; break; }
                    if (index < 0 && c.DropDownStyle == ComboBoxStyle.DropDownList)
                        index = c.Items.Add(display); // keep unknown values instead of silently changing them
                    if (index >= 0) c.SelectedIndex = index; else c.Text = display;
                    break;
                default:
                    m_editor.Text = value;
                    break;
            }
            m_suppress = false;
        }

        void OnEditorChanged()
        {
            if (m_suppress) return;
            ResetRequested = false;
            UpdateState();
            ValueChanged?.Invoke(this, EventArgs.Empty);
        }

        void RequestReset()
        {
            SetEditorValue(Setting.BaselineValue ?? "");
            // with a separate user INI, resetting removes the override so the global value applies again
            ResetRequested = m_canDeleteKey && Setting.UserValue != null;
            UpdateState();
            ValueChanged?.Invoke(this, EventArgs.Empty);
        }

        void UpdateState()
        {
            bool changed = IsChangedFromDefault;
            m_title.ForeColor = changed ? AccentColor : Color.Black;
            m_key.Text = Setting.Key + (changed ? "   • changed (default: " + Shorten(Setting.BaselineValue) + ")" : "");
            bool overridden = m_canDeleteKey && Setting.UserValue != null;
            m_reset.Visible = Setting.BaselineValue != null && (changed || overridden) && !ResetRequested;
            if (m_editor is TextBox || (m_editor is ComboBox cb && cb.DropDownStyle == ComboBoxStyle.DropDown))
                m_editor.BackColor = IsValid ? SystemColors.Window : Color.FromArgb(255, 220, 220);
            if (Setting.Kind == SettingKind.Color && m_pickButton != null)
                m_pickButton.BackColor = ParseColor(CurrentValue) ?? SystemColors.Control;
        }

        static string Shorten(string s) => s.Length == 0 ? "empty" : s.Length > 40 ? s.Substring(0, 37) + "..." : s;

        public void LayoutRow(int width)
        {
            SuspendLayout();
            Width = width;
            int pad = S(12);
            int editorWidth = Math.Min(S(320), Math.Max(S(160), width / 3));
            int editorLeft = width - editorWidth - pad;
            int textWidth = editorLeft - 2 * pad;

            m_title.Location = new Point(pad, S(10));
            m_key.Location = new Point(pad, m_title.Bottom + S(1));
            m_key.MaximumSize = new Size(textWidth, 0);
            m_description.MaximumSize = new Size(textWidth, 0);
            m_description.Location = new Point(pad, m_key.Bottom + S(6));
            m_perf.MaximumSize = new Size(textWidth, 0);
            m_perf.Location = new Point(pad, (m_description.Text.Length > 0 ? m_description.Bottom : m_key.Bottom) + S(6));

            int pickWidth = m_pickButton == null ? 0 : (Setting.Kind == SettingKind.Color ? S(36) : S(80));
            m_editor.Location = new Point(editorLeft, S(10));
            if (!(m_editor is CheckBox)) m_editor.Width = editorWidth - (pickWidth > 0 ? pickWidth + S(6) : 0);
            if (m_pickButton != null)
            {
                m_pickButton.Bounds = new Rectangle(m_editor.Right + S(6), m_editor.Top, pickWidth, m_editor.Height);
            }
            m_reset.Location = new Point(editorLeft, m_editor.Bottom + S(6));

            int bottom = Math.Max(m_perf.Visible ? m_perf.Bottom : (m_description.Text.Length > 0 ? m_description.Bottom : m_key.Bottom), m_reset.Bottom);
            Height = bottom + S(12);
            ResumeLayout();
        }

        void Pick()
        {
            if (Setting.Kind == SettingKind.Color)
            {
                using (var dlg = new ColorDialog { FullOpen = true, Color = ParseColor(CurrentValue) ?? Color.Black })
                {
                    if (dlg.ShowDialog(FindForm()) == DialogResult.OK)
                    {
                        m_editor.Text = $"{dlg.Color.R} {dlg.Color.G} {dlg.Color.B}";
                    }
                }
            }
            else
            {
                using (var dlg = new FontDialog { ShowEffects = false, FontMustExist = true })
                {
                    var current = ParseFont(CurrentValue);
                    if (current != null) dlg.Font = current;
                    if (dlg.ShowDialog(FindForm()) == DialogResult.OK)
                    {
                        Font f = dlg.Font;
                        m_editor.Text = string.Format(CultureInfo.InvariantCulture, "\"{0}\" {1:0.0}{2}", f.Name, f.SizeInPoints, f.Bold ? " bold" : "");
                    }
                }
            }
        }

        static Color? ParseColor(string value)
        {
            var parts = value.Split(new[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length != 3) return null;
            if (!int.TryParse(parts[0], out int r) || !int.TryParse(parts[1], out int g) || !int.TryParse(parts[2], out int b)) return null;
            if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) return null;
            return Color.FromArgb(r, g, b);
        }

        static Font ParseFont(string value)
        {
            var m = Regex.Match(value, "^\\s*\"([^\"]+)\"\\s+([0-9.]+)\\s*(bold)?", RegexOptions.IgnoreCase);
            if (!m.Success) return null;
            if (!float.TryParse(m.Groups[2].Value, NumberStyles.Float, CultureInfo.InvariantCulture, out float size)) return null;
            try { return new Font(m.Groups[1].Value, size, m.Groups[3].Success ? FontStyle.Bold : FontStyle.Regular); }
            catch (ArgumentException) { return null; }
        }

        // "ShowEXIFDateInTitle" -> "Show EXIF date in title"
        static string Humanize(string key)
        {
            string spaced = Regex.Replace(key, @"(?<=[a-z0-9])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])", " ");
            var words = spaced.Split(' ');
            for (int i = 1; i < words.Length; i++)
            {
                bool acronym = words[i].Length > 1 && words[i].All(char.IsUpper);
                if (!acronym) words[i] = words[i].ToLowerInvariant();
            }
            return string.Join(" ", words);
        }
    }
}
