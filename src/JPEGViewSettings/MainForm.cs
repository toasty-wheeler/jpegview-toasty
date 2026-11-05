using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Linq;
using System.Windows.Forms;

namespace JPEGViewSettings
{
    public class MainForm : Form
    {
        const string AllCategory = "All settings";
        const string PerformanceCategory = "⚡ Performance";
        const string ChangedCategory = "Changed from default";

        readonly IniModel m_model;
        readonly List<SettingRow> m_rows = new List<SettingRow>();
        readonly ListBox m_categories;
        readonly TextBox m_search;
        readonly FlowLayoutPanel m_list;
        readonly Label m_status, m_noResults;
        readonly Button m_save;
        readonly float m_scale;

        int S(int v) => (int)Math.Round(v * m_scale);

        public MainForm(IniModel model)
        {
            m_model = model;
            Font = SystemFonts.MessageBoxFont;
            using (var g = CreateGraphics()) m_scale = g.DpiX / 96f;

            Text = "JPEGView Settings";
            Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath);
            Size = new Size(S(1000), S(720));
            MinimumSize = new Size(S(720), S(480));
            StartPosition = FormStartPosition.CenterScreen;
            KeyPreview = true;

            // --- top: search and file info
            var top = new Panel { Dock = DockStyle.Top, Height = S(64), Padding = new Padding(S(12), S(10), S(12), S(6)) };
            m_search = new TextBox { Dock = DockStyle.Top };
            SetCueBanner(m_search, "Search settings (name, key or description)   Ctrl+F");
            m_search.TextChanged += (s, e) => ApplyFilter();
            var fileInfo = new Label
            {
                Dock = DockStyle.Bottom, AutoSize = false, Height = S(20), ForeColor = Color.FromArgb(96, 96, 96),
                Text = model.SingleFile
                    ? "Editing: " + model.UserIniPath
                    : "Saving to: " + model.UserIniPath + "    (defaults from " + model.GlobalIniPath + ")",
                AutoEllipsis = true
            };
            top.Controls.Add(m_search);
            top.Controls.Add(fileInfo);

            // --- left: categories
            m_categories = new ListBox
            {
                Dock = DockStyle.Left, Width = S(220), IntegralHeight = false, BorderStyle = BorderStyle.None,
                DrawMode = DrawMode.OwnerDrawFixed, ItemHeight = S(28), BackColor = Color.FromArgb(243, 243, 243)
            };
            m_categories.Items.Add(AllCategory);
            m_categories.Items.Add(PerformanceCategory);
            m_categories.Items.Add(ChangedCategory);
            foreach (var c in model.Categories) m_categories.Items.Add(c);
            m_categories.DrawItem += DrawCategory;
            m_categories.SelectedIndexChanged += (s, e) => ApplyFilter();

            // --- center: setting rows
            m_list = new FlowLayoutPanel
            {
                Dock = DockStyle.Fill, AutoScroll = true, FlowDirection = FlowDirection.TopDown, WrapContents = false,
                BackColor = Color.FromArgb(229, 229, 229), Padding = new Padding(0)
            };
            m_noResults = new Label { AutoSize = true, Text = "No settings match.", Padding = new Padding(S(16)), Visible = false };
            m_list.Controls.Add(m_noResults);
            m_list.SuspendLayout();
            foreach (var setting in model.Settings)
            {
                var row = new SettingRow(setting, !model.SingleFile, Font, S);
                row.ValueChanged += (s, e) => UpdateStatus();
                m_rows.Add(row);
                m_list.Controls.Add(row);
            }
            m_list.ResumeLayout();
            m_list.SizeChanged += (s, e) => LayoutRows();

            // --- bottom: status and buttons
            var bottom = new Panel { Dock = DockStyle.Bottom, Height = S(52), Padding = new Padding(S(12), S(10), S(12), S(10)) };
            var close = new Button { Text = "Close", Dock = DockStyle.Right, Width = S(96) };
            close.Click += (s, e) => Close();
            m_save = new Button { Text = "Save", Dock = DockStyle.Right, Width = S(96), Enabled = false };
            m_save.Click += (s, e) => Save();
            var asText = new Button { Text = "Open as text", Dock = DockStyle.Right, Width = S(120) };
            asText.Click += (s, e) => OpenAsText();
            m_status = new Label { Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft, AutoEllipsis = true };
            bottom.Controls.Add(m_status);
            bottom.Controls.Add(asText);
            bottom.Controls.Add(new Panel { Dock = DockStyle.Right, Width = S(8) });
            bottom.Controls.Add(m_save);
            bottom.Controls.Add(new Panel { Dock = DockStyle.Right, Width = S(8) });
            bottom.Controls.Add(close);

            Controls.Add(m_list);
            Controls.Add(new Panel { Dock = DockStyle.Left, Width = 1, BackColor = Color.FromArgb(210, 210, 210) });
            Controls.Add(m_categories);
            Controls.Add(top);
            Controls.Add(bottom);
            AcceptButton = null;

            m_categories.SelectedIndex = 0;
            UpdateStatus();
        }

        protected override void OnShown(EventArgs e)
        {
            base.OnShown(e);
            LayoutRows();
            m_search.Focus();
        }

        protected override bool ProcessCmdKey(ref Message msg, Keys keyData)
        {
            if (keyData == (Keys.Control | Keys.F)) { m_search.Focus(); m_search.SelectAll(); return true; }
            if (keyData == (Keys.Control | Keys.S)) { if (m_save.Enabled) Save(); return true; }
            return base.ProcessCmdKey(ref msg, keyData);
        }

        protected override void OnFormClosing(FormClosingEventArgs e)
        {
            int dirty = m_rows.Count(r => r.IsDirty);
            if (dirty > 0 && e.CloseReason == CloseReason.UserClosing)
            {
                var answer = MessageBox.Show(this, $"Save {dirty} changed setting(s) before closing?", Text,
                    MessageBoxButtons.YesNoCancel, MessageBoxIcon.Question);
                if (answer == DialogResult.Cancel || (answer == DialogResult.Yes && !Save())) e.Cancel = true;
            }
            base.OnFormClosing(e);
        }

        void DrawCategory(object sender, DrawItemEventArgs e)
        {
            if (e.Index < 0) return;
            bool selected = (e.State & DrawItemState.Selected) != 0;
            using (var back = new SolidBrush(selected ? Color.FromArgb(204, 228, 247) : m_categories.BackColor))
                e.Graphics.FillRectangle(back, e.Bounds);
            string text = (string)m_categories.Items[e.Index];
            if (text == ChangedCategory) text += $" ({m_rows.Count(r => r.IsChangedFromDefault)})";
            var bounds = new Rectangle(e.Bounds.X + S(12), e.Bounds.Y, e.Bounds.Width - S(12), e.Bounds.Height);
            bool special = e.Index < 3;
            using (var font = new Font(Font, special ? FontStyle.Bold : FontStyle.Regular))
                TextRenderer.DrawText(e.Graphics, text, font, bounds, Color.Black, TextFormatFlags.VerticalCenter | TextFormatFlags.Left | TextFormatFlags.EndEllipsis);
            if (e.Index == 2)
                using (var pen = new Pen(Color.FromArgb(210, 210, 210)))
                    e.Graphics.DrawLine(pen, e.Bounds.Left + S(8), e.Bounds.Bottom - 1, e.Bounds.Right - S(8), e.Bounds.Bottom - 1);
        }

        void ApplyFilter()
        {
            string category = m_categories.SelectedItem as string ?? AllCategory;
            string filter = m_search.Text.Trim();
            m_list.SuspendLayout();
            int visible = 0;
            foreach (var row in m_rows)
            {
                bool inCategory =
                    filter.Length > 0 || category == AllCategory ||
                    (category == PerformanceCategory && row.Setting.PerformanceHint != null) ||
                    (category == ChangedCategory && row.IsChangedFromDefault) ||
                    row.Setting.Category == category;
                // count with a local: Control.Visible reads false while the form itself is not shown yet
                bool show = inCategory && row.Matches(filter);
                row.Visible = show;
                if (show) visible++;
            }
            m_noResults.Visible = visible == 0;
            m_list.ResumeLayout();
            m_list.AutoScrollPosition = new Point(0, 0);
        }

        void LayoutRows()
        {
            int width = m_list.ClientSize.Width - (m_list.VerticalScroll.Visible ? 0 : SystemInformation.VerticalScrollBarWidth);
            if (width <= 0) return;
            m_list.SuspendLayout();
            foreach (var row in m_rows) row.LayoutRow(width);
            m_list.ResumeLayout();
        }

        void UpdateStatus()
        {
            int dirty = m_rows.Count(r => r.IsDirty);
            int invalid = m_rows.Count(r => r.IsDirty && !r.IsValid);
            m_save.Enabled = dirty > 0;
            Text = (dirty > 0 ? "* " : "") + "JPEGView Settings";
            m_status.ForeColor = invalid > 0 ? Color.DarkRed : SystemColors.ControlText;
            m_status.Text = invalid > 0 ? $"{invalid} value(s) are not valid numbers." :
                dirty > 0 ? $"{dirty} unsaved change(s)." : "";
            m_categories.Invalidate();
        }

        bool Save()
        {
            var invalid = m_rows.Where(r => r.IsDirty && !r.IsValid).Select(r => r.Setting.Key).ToList();
            if (invalid.Count > 0)
            {
                MessageBox.Show(this, "These settings need a number:\n\n" + string.Join("\n", invalid), Text, MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return false;
            }
            try
            {
                foreach (var row in m_rows.Where(r => r.IsDirty))
                {
                    m_model.Write(row.Setting, row.ResetRequested ? null : row.CurrentValue);
                    row.AcceptSaved();
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, ex.Message, Text, MessageBoxButtons.OK, MessageBoxIcon.Error);
                return false;
            }
            UpdateStatus();
            bool running = Process.GetProcessesByName("JPEGView").Length > 0;
            m_status.Text = running ? "Saved. Close and reopen JPEGView to apply the changes." : "Saved.";
            return true;
        }

        void OpenAsText()
        {
            try { Process.Start("notepad.exe", "\"" + m_model.UserIniPath + "\""); }
            catch (Exception ex) { MessageBox.Show(this, ex.Message, Text, MessageBoxButtons.OK, MessageBoxIcon.Error); }
        }

        [System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
        static extern IntPtr SendMessage(IntPtr hWnd, int msg, IntPtr wParam, string lParam);

        static void SetCueBanner(TextBox box, string text)
        {
            const int EM_SETCUEBANNER = 0x1501;
            box.HandleCreated += (s, e) => SendMessage(box.Handle, EM_SETCUEBANNER, (IntPtr)1, text);
        }
    }
}
