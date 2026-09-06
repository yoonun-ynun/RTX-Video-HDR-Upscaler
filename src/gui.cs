using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using System.Windows.Forms;

internal sealed class HdrWindow : Form
{
    internal readonly TextBox Input = new TextBox(), Output = new TextBox();
    internal readonly ComboBox mode = new ComboBox();
    internal readonly ComboBox Gpu = new ComboBox(), FormatChoice = new ComboBox();
    sealed class GpuChoice { internal int Index; internal string Name; public override string ToString() {return "GPU " + Index + " · " + Name;} }
    readonly NumericUpDown bitrate = new NumericUpDown(), cq = new NumericUpDown();
    internal readonly CheckBox Checkpoint = new CheckBox();
    readonly Label checkpointHint = new Label();
    bool activeCheckpoint, rememberedCheckpoint;
    readonly CheckBox preview = new CheckBox(), assume = new CheckBox();
    readonly Button resume = new Button();
    readonly Button start = new Button(), cancel = new Button(), play = new Button(), folder = new Button();
    readonly Button chooseInput = new Button(), chooseOutput = new Button();
    readonly TextBox log = new TextBox();
    readonly ProgressBar progress = new ProgressBar();
    internal readonly Label Status = new Label();
    readonly Label detail = new Label();
    internal Process Running;
    internal bool Finished, Succeeded, SawProgress;
    internal string LastProgress = "";
    internal readonly List<string> StageHistory = new List<string>();
    int processingStage;
    internal string Diagnostics { get { return detail.Text + "\n" + log.Text; } }
    bool cancelled, closing;
    bool restoringSettings = true;
    readonly string settingsPath;
    GuiSettings savedSettings;
    string settingsError = "";
    string completedOutput = "", logDirectory = "";
    readonly Color ink = Color.FromArgb(30, 37, 54), accent = Color.FromArgb(89, 68, 213);

    internal HdrWindow(string settingsFile = null)
    {
        settingsPath = settingsFile ?? Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"settings.ini");
        Text = "RTX Video HDR 업스케일러 · SDR → HDR";
        Font = new Font("맑은 고딕", 10F);
        AutoScaleMode = AutoScaleMode.None;
        ClientSize = new Size(900, 786); MinimumSize = new Size(916, 825);
        StartPosition = FormStartPosition.CenterScreen;
        BackColor = Color.FromArgb(245, 246, 250); ForeColor = ink;
        AllowDrop = true;
        Label title = LabelAt("RTX Video HDR 업스케일러", 28, 16, 844, 38);
        title.Font = new Font(Font.FontFamily, 22F, FontStyle.Bold);
        LabelAt("NVIDIA RTX Video HDR로 SDR 영상을 HDR로 업스케일링합니다.", 30, 61, 830, 24);
        LabelAt("원본 해상도·프레임률 유지  ·  HEVC 10비트  ·  MKV / MP4 저장", 30, 85, 830, 24);

        GroupBox files = Group("01   영상 파일", 28, 111, 844, 156);
        AddLabel(files, "원본 영상", 18, 32, 95);
        SetupText(Input, files, 118, 29, 595);
        SetupButton(chooseInput, files, "찾아보기", 724, 27, 102, delegate { PickInput(); });
        AddLabel(files, "저장 위치", 18, 77, 95);
        SetupText(Output, files, 118, 74, 595);
        SetupButton(chooseOutput, files, "변경", 724, 72, 102, delegate { PickOutput(); });
        AddLabel(files, "영상을 바꾸면 새 원본 옆의 HDR 파일명으로 저장 위치가 자동 변경됩니다.", 118, 119, 700);
        Input.TextChanged += delegate {
            try {
                string input = Clean(Input.Text);
                string next = String.IsNullOrWhiteSpace(input) ? "" : Path.Combine(Path.GetDirectoryName(input) ?? "", Path.GetFileNameWithoutExtension(input) + (FormatChoice.SelectedIndex==1?".hdr.mp4":".hdr.mkv"));
                Output.Text = next;
            } catch(ArgumentException) { Output.Clear(); } catch(PathTooLongException) { Output.Clear(); }
        };

        GroupBox settings = Group("02   출력 품질 · 재개 설정", 28, 280, 844, 218);
        AddLabel(settings, "인코딩 방식", 18, 34, 95);
        mode.DropDownStyle = ComboBoxStyle.DropDownList;
        mode.Items.AddRange(new object[] { "품질 기준 (CQ)", "평균 비트레이트 (VBR)" });
        mode.SetBounds(118, 30, 228, 32); settings.Controls.Add(mode);
        AddLabel(settings, "CQ", 370, 35, 38);
        cq.Minimum = 0; cq.Maximum = 51; cq.Value = 18; cq.SetBounds(414, 31, 80, 32); settings.Controls.Add(cq);
        AddLabel(settings, "Mbps", 535, 35, 60);
        bitrate.Minimum = 1; bitrate.Maximum = 1000; bitrate.DecimalPlaces = 1; bitrate.Value = 40;
        bitrate.SetBounds(598, 31, 100, 32); settings.Controls.Add(bitrate);
        mode.SelectedIndexChanged += delegate { cq.Enabled = mode.SelectedIndex == 0 && Running == null; bitrate.Enabled = mode.SelectedIndex == 1 && Running == null; };
        mode.SelectedIndex = 0;
        AddLabel(settings, "사용할 GPU", 18, 108, 95);
        Gpu.DropDownStyle=ComboBoxStyle.DropDownList;Gpu.SetBounds(118,103,325,32);settings.Controls.Add(Gpu);
        AddLabel(settings,"저장 형식",465,108,80);
        FormatChoice.DropDownStyle=ComboBoxStyle.DropDownList;FormatChoice.SetBounds(548,103,278,32);
        FormatChoice.Items.AddRange(new object[]{"MKV · 오디오 원본 복사","MP4 · 오디오 AAC 변환"});settings.Controls.Add(FormatChoice);
        FormatChoice.SelectedIndexChanged += delegate {
            try {if(!String.IsNullOrWhiteSpace(Output.Text)) Output.Text=Path.ChangeExtension(Clean(Output.Text),FormatChoice.SelectedIndex==1?".mp4":".mkv");} catch(ArgumentException) {}
        };
        FormatChoice.SelectedIndex=0;
        Shown += delegate {
            LoadGpus();
            int match=-1;
            for(int i=0;i<Gpu.Items.Count;i++) {
                GpuChoice choice=(GpuChoice)Gpu.Items[i];
                if(choice.Index==savedSettings.GpuIndex && choice.Name==savedSettings.GpuName) {match=i;break;}
            }
            // If enumeration changed, only use a name match when it is unambiguous.
            if(match<0) {
                int matches=0;
                for(int i=0;i<Gpu.Items.Count;i++) if(((GpuChoice)Gpu.Items[i]).Name==savedSettings.GpuName) {match=i;matches++;}
                if(matches!=1) match=-1;
            }
            if(match>=0) Gpu.SelectedIndex=match;
            restoringSettings=false;
            SaveSettings();
        };
        AddLabel(settings, "CQ는 낮을수록 높은 품질을 지향합니다. VBR은 입력한 평균 비트레이트를 목표로 합니다.", 118, 71, 710);
        preview.Text = "시험 변환: 첫 432프레임"; preview.SetBounds(118, 145, 258, 25); settings.Controls.Add(preview);
        assume.Text = "색 정보가 없는 SDR을 BT.709로 간주"; assume.SetBounds(392, 145, 414, 25); settings.Controls.Add(assume);

        Checkpoint.Text="새 변환에서 구간 저장 (재개 지원)";
        Checkpoint.SetBounds(118, 180, 335, 25);settings.Controls.Add(Checkpoint);
        checkpointHint.SetBounds(460, 181, 366, 25);settings.Controls.Add(checkpointHint);

        SetupButton(start, this, "HDR 업스케일링 시작", 28, 515, 198, delegate { StartConversion(); });
        start.BackColor = accent; start.ForeColor = Color.White; start.FlatStyle = FlatStyle.Flat; start.FlatAppearance.BorderSize = 0;
        SetupButton(cancel, this, "취소", 238, 515, 95, delegate { CancelConversion(); }); cancel.Enabled = false;
        SetupButton(resume, this, "이어서 변환", 346, 515, 165, delegate { PickResume(); });
        SetupButton(play, this, "결과 재생", 646, 515, 108, delegate { OpenPath(completedOutput); }); play.Enabled = false;
        SetupButton(folder, this, "저장 폴더", 766, 515, 106, delegate { OpenPath(Path.GetDirectoryName(completedOutput)); }); folder.Enabled = false;
        play.Anchor = folder.Anchor = AnchorStyles.Top | AnchorStyles.Right;
        Status.SetBounds(28, 565, 844, 26); Status.Text = "변환할 영상을 선택하세요"; Status.Font = new Font(Font, FontStyle.Bold); Controls.Add(Status);
        progress.SetBounds(28, 601, 844, 10); progress.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
        progress.Maximum = 1000; Controls.Add(progress);
        detail.SetBounds(28, 624, 844, 25); detail.Text = "하드웨어 디코딩과 GPU 색 변환을 사용합니다."; Controls.Add(detail);
        log.SetBounds(28, 661, 844, 97); log.Multiline = true; log.ReadOnly = true; log.ScrollBars = ScrollBars.Vertical;
        log.BackColor = Color.White; log.Font = new Font("Consolas", 9F); log.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
        Controls.Add(log);
        DragEnter += delegate(object sender, DragEventArgs e) { e.Effect = Running == null && e.Data.GetDataPresent(DataFormats.FileDrop) ? DragDropEffects.Copy : DragDropEffects.None; };
        DragDrop += delegate(object sender, DragEventArgs e) {
            var paths = e.Data.GetData(DataFormats.FileDrop) as string[];
            if(Running == null && paths != null && paths.Length == 1) Input.Text = paths[0];
        };
        FormClosing += delegate(object sender, FormClosingEventArgs e) {
            SaveSettings();
            if(Running != null) { e.Cancel = true; closing = true; CancelConversion(); }
        };
        try {savedSettings=GuiSettings.Load(settingsPath);}
        catch(Exception e) {savedSettings=new GuiSettings();log.AppendText("설정 파일을 읽을 수 없어 기본값으로 시작합니다: "+e.Message+Environment.NewLine);}
        mode.SelectedIndex=savedSettings.Mode;
        cq.Value=savedSettings.Cq;bitrate.Value=savedSettings.Bitrate;
        FormatChoice.SelectedIndex=savedSettings.Format;
        Checkpoint.Checked=savedSettings.Checkpoint;UpdateCheckpointHint();
        Checkpoint.CheckedChanged += delegate {UpdateCheckpointHint();SaveSettings();};
        mode.SelectedIndexChanged += delegate {SaveSettings();};
        cq.ValueChanged += delegate {SaveSettings();};
        bitrate.ValueChanged += delegate {SaveSettings();};
        FormatChoice.SelectedIndexChanged += delegate {SaveSettings();};
        Gpu.SelectedIndexChanged += delegate {SaveSettings();};
        using(Graphics g=CreateGraphics()) {
            float scale=g.DpiX/96F;
            if(scale!=1F) Scale(new SizeF(scale,scale));
        }
    }
    void UpdateCheckpointHint() {
        checkpointHint.Text=Checkpoint.Checked?"영상 약 10초마다 저장 · 추가 처리 비용":"속도 우선 · 중단한 작업은 재개 불가";
    }
    void SaveSettings() {
        if(restoringSettings) return;
        savedSettings.Checkpoint=Checkpoint.Checked;
        savedSettings.Mode=mode.SelectedIndex;savedSettings.Cq=cq.Value;
        savedSettings.Bitrate=bitrate.Value;savedSettings.Format=FormatChoice.SelectedIndex;
        GpuChoice choice=Gpu.SelectedItem as GpuChoice;
        if(choice!=null) {savedSettings.GpuIndex=choice.Index;savedSettings.GpuName=choice.Name;}
        try {savedSettings.Save(settingsPath);settingsError="";}
        catch(Exception e) {
            if(settingsError!=e.Message) log.AppendText("설정 저장 실패: "+e.Message+Environment.NewLine);
            settingsError=e.Message;
        }
    }
    Label LabelAt(string text, int x, int y, int w, int h) { Label l = new Label { Text = text }; l.SetBounds(x,y,w,h); Controls.Add(l); return l; }
    void AddLabel(Control parent, string text, int x, int y, int w) { Label l = new Label { Text = text }; l.SetBounds(x,y,w,25); parent.Controls.Add(l); }
    GroupBox Group(string text,int x,int y,int w,int h) {
        GroupBox box = new GroupBox { Text = text, BackColor = Color.White };
        box.SetBounds(x,y,w,h); box.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right; Controls.Add(box); return box;
    }
    void SetupText(TextBox t, Control parent,int x,int y,int w) {t.SetBounds(x,y,w,30);t.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right; parent.Controls.Add(t);}
    void SetupButton(Button b, Control parent,string text,int x,int y,int w,EventHandler action) {
        b.Text = text; b.SetBounds(x,y,w,36); b.UseVisualStyleBackColor = true; b.Click += action; parent.Controls.Add(b);
        if(parent is GroupBox) b.Anchor = AnchorStyles.Top | AnchorStyles.Right;
    }
    internal static bool RuntimeFileAvailable(string name) {
        if(File.Exists(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,name)))return true;
        foreach(string entry in (Environment.GetEnvironmentVariable("PATH") ?? "").Split(Path.PathSeparator)) {
            string directory=Environment.ExpandEnvironmentVariables(entry.Trim().Trim('"'));
            if(String.IsNullOrWhiteSpace(directory))continue;
            try {if(File.Exists(Path.Combine(directory,name)))return true;}
            catch(ArgumentException) {} catch(NotSupportedException) {}
        }
        return false;
    }
    bool MissingRuntime() {
        if(!File.Exists(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"native-runtime.required")))return false;
        foreach(string name in new string[]{"avcodec-62.dll","avformat-62.dll","avutil-60.dll","swresample-6.dll","ffmpeg.exe","ffprobe.exe"})
            if(!RuntimeFileAvailable(name))return true;
        return false;
    }
    void LoadGpus() {
        if(MissingRuntime()) {
            bool toolsFound=RuntimeFileAvailable("ffmpeg.exe") && RuntimeFileAvailable("ffprobe.exe");
            start.Text=toolsFound?"GPU 처리 DLL 추가":"필수 구성 설치";
            Status.Text=toolsFound?"FFmpeg 도구 확인 완료 · GPU 처리용 공유 DLL이 필요합니다":"처음 실행: FFmpeg 구성 설치가 필요합니다";
            detail.Text=toolsFound?"기존 FFmpeg를 사용합니다. GPU 직접 처리를 위한 DLL만 추가로 설치하세요.":"설치 버튼을 누르면 필요한 구성을 내려받습니다. 기존 FFmpeg 도구는 유지합니다.";
            return;
        }
        try {
            using(Process p = new Process {StartInfo=new ProcessStartInfo(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"RTXVideoHDRConvert.exe"),"--list-gpus") {
                UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true,
                StandardOutputEncoding=Encoding.UTF8,StandardErrorEncoding=Encoding.UTF8
            }}) {
                p.Start();string data=p.StandardOutput.ReadToEnd();string error=p.StandardError.ReadToEnd();p.WaitForExit();
                if(p.ExitCode!=0) throw new Exception(error);
                foreach(string line in data.Split('\n')) {
                    string[] fields=line.Trim().Split('\t');int index;
                    if(fields.Length==2 && Int32.TryParse(fields[0],out index)) Gpu.Items.Add(new GpuChoice {Index=index,Name=fields[1]});
                }
                if(Gpu.Items.Count==0) throw new Exception("사용 가능한 NVIDIA GPU가 없습니다.");
                Gpu.SelectedIndex=0;
            }
        } catch(Exception e) {start.Enabled=false;Status.Text="GPU를 확인할 수 없습니다";detail.Text=e.Message;}
    }
    static string Clean(string text) { return text.Trim().Trim('"'); }
    void PickInput() {using(OpenFileDialog d = new OpenFileDialog {Filter = "영상 파일|*.mp4;*.mkv;*.mov;*.ts|모든 파일|*.*", Title = "SDR 영상 선택"}) if(d.ShowDialog(this)==DialogResult.OK) Input.Text=d.FileName;}
    void PickOutput() {using(SaveFileDialog d = new SaveFileDialog {Filter=FormatChoice.SelectedIndex==1?"MP4 HDR 영상|*.mp4":"MKV HDR 영상|*.mkv",DefaultExt=FormatChoice.SelectedIndex==1?"mp4":"mkv",FileName=Path.GetFileName(Clean(Output.Text)),OverwritePrompt=true}) if(d.ShowDialog(this)==DialogResult.OK) Output.Text=d.FileName;}
    static string Quote(string value) {
        StringBuilder b = new StringBuilder("\""); int slashes = 0;
        foreach(char c in value) {
            if(c == '\\') {slashes++; continue;}
            b.Append('\\', c == '"' ? slashes*2+1 : slashes); b.Append(c); slashes=0;
        }
        b.Append('\\',slashes*2); b.Append('"'); return b.ToString();
    }
    void Busy(bool busy) {
        foreach(Control c in new Control[] {Input,Output,chooseInput,chooseOutput,mode,Gpu,FormatChoice,preview,assume,Checkpoint,start}) c.Enabled=!busy;
        cq.Enabled=!busy && mode.SelectedIndex==0; bitrate.Enabled=!busy && mode.SelectedIndex==1;
        resume.Enabled=!busy;cancel.Enabled=busy; play.Enabled=folder.Enabled=!busy && Succeeded;
        UseWaitCursor=false;
    }
    void PickResume() {
        using(var picker=new OpenFileDialog()) {
            picker.Title="이어서 변환할 작업의 checkpoint.txt 선택";
            picker.Filter="HDR 작업 체크포인트|checkpoint.txt";
            string recent=Path.Combine(Path.GetDirectoryName(settingsPath),"last-checkpoint.txt");
            try {if(File.Exists(recent)) {string path=File.ReadAllText(recent,Encoding.UTF8).Trim();if(File.Exists(path)){picker.InitialDirectory=Path.GetDirectoryName(path);picker.FileName=path;}}}catch(IOException){}
            if(picker.ShowDialog(this)==DialogResult.OK) StartConversion(picker.FileName);
        }
    }
    internal void StartConversion(string checkpoint = null) {
        if(MissingRuntime()) {
            try {Process.Start(new ProcessStartInfo(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"Setup-Runtime.cmd")){UseShellExecute=true});Close();}
            catch(Exception e) {Status.Text="구성 설치를 시작하지 못했습니다";detail.Text=e.Message;}
            return;
        }
        if(Running != null) return;
        try {
            string engine=Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"RTXVideoHDRConvert.exe");
            if(!File.Exists(engine)) throw new Exception("같은 폴더에 RTXVideoHDRConvert.exe가 필요합니다.");
            string output="",arguments="";
            if(checkpoint!=null) {
                if(!File.Exists(checkpoint))throw new Exception("체크포인트를 찾을 수 없습니다.");
                arguments="--resume "+Quote(Path.GetFullPath(checkpoint));
            } else {
            string input=Path.GetFullPath(Clean(Input.Text));output=Path.GetFullPath(Clean(Output.Text));
            if(!File.Exists(input)) throw new Exception("원본 영상 파일을 찾을 수 없습니다.");
            string extension=FormatChoice.SelectedIndex==1?".mp4":".mkv";
            if(!output.EndsWith(extension,StringComparison.OrdinalIgnoreCase)) throw new Exception("저장 파일 확장자를 "+extension+"로 지정하세요.");
            GpuChoice selected=Gpu.SelectedItem as GpuChoice;
            if(selected==null) throw new Exception("사용 가능한 NVIDIA GPU를 선택하세요.");
            if(File.Exists(output)) throw new Exception("저장 위치에 파일이 이미 있습니다. 다른 이름을 지정하세요.");
            arguments=Quote(input)+" --output "+Quote(output)+" --adapter "+selected.Index;
            arguments += mode.SelectedIndex==0 ? " --cq "+cq.Value.ToString(CultureInfo.InvariantCulture) : " --bitrate "+bitrate.Value.ToString(CultureInfo.InvariantCulture)+"M";
            if(preview.Checked) arguments+=" --max-frames 432";
            if(assume.Checked) arguments+=" --assume-bt709";
            if(!Checkpoint.Checked) arguments+=" --no-checkpoint";
            }
            Process p = new Process { StartInfo=new ProcessStartInfo(engine,arguments) {
                UseShellExecute=false, CreateNoWindow=true, RedirectStandardOutput=true, RedirectStandardError=true,
                StandardOutputEncoding=Encoding.UTF8, StandardErrorEncoding=Encoding.UTF8,
                WorkingDirectory=AppDomain.CurrentDomain.BaseDirectory
            }};
            // Inherit the native environment without rebuilding .NET's case-insensitive
            // dictionary: some launchers supply both Path and PATH.
            activeCheckpoint=checkpoint!=null||Checkpoint.Checked;rememberedCheckpoint=false;
            Running=p; Finished=Succeeded=SawProgress=cancelled=false; completedOutput=output; logDirectory="";
            processingStage=0;StageHistory.Clear();LastProgress="";
            log.Clear(); Busy(true); progress.Style=ProgressBarStyle.Marquee; Status.Text="입력 확인 및 HDR 준비 중…";
            detail.Text="전체 영상을 사전 디코딩하지 않고 변환 중에 검사합니다.";
            p.OutputDataReceived += delegate(object s,DataReceivedEventArgs e) { if(e.Data!=null) Dispatch(delegate {if(Running==p) Receive(e.Data);}); };
            p.ErrorDataReceived += delegate(object s,DataReceivedEventArgs e) { if(e.Data!=null) Dispatch(delegate {if(Running==p) Receive(e.Data);}); };
            p.Start(); p.BeginOutputReadLine(); p.BeginErrorReadLine();
            System.Threading.Thread waiter=new System.Threading.Thread(delegate() {
                p.WaitForExit(); int code=p.ExitCode;
                Dispatch(delegate {if(Running==p) Complete(code);});
            }); waiter.IsBackground=true; waiter.Start();
        } catch(Exception ex) {
            if(Running!=null) {try {if(!Running.HasExited) Running.Kill();} catch(InvalidOperationException) {} Running.Dispose(); Running=null;}
            Finished=true;Succeeded=false;Busy(false);progress.Style=ProgressBarStyle.Blocks;progress.Value=0;
            Status.Text="시작할 수 없습니다"; detail.Text=ex.Message; log.AppendText(ex.Message+Environment.NewLine);
        }
    }
    void Dispatch(Action action) { if(!IsDisposed && IsHandleCreated) {try {BeginInvoke(action);} catch(InvalidOperationException) {}} }
    void Receive(string line) {
        if(line.StartsWith("RTXHDR_INPUT ")) {Input.Text=line.Substring(13).Trim();return;}
        if(line.StartsWith("RTXHDR_OUTPUT ")) {completedOutput=line.Substring(14).Trim();FormatChoice.SelectedIndex=completedOutput.EndsWith(".mp4",StringComparison.OrdinalIgnoreCase)?1:0;Output.Text=completedOutput;return;}
        if(line.StartsWith("RTXHDR_JOB_SETTINGS ")) {
            string[] fields=line.Substring(20).Split(' ');int index,quality,maximum;decimal bits;
            if(fields.Length==5&&Int32.TryParse(fields[0],out index)&&Int32.TryParse(fields[1],out quality)&&Decimal.TryParse(fields[2],NumberStyles.None,CultureInfo.InvariantCulture,out bits)&&Int32.TryParse(fields[4],out maximum)) {
                for(int i=0;i<Gpu.Items.Count;i++)if(((GpuChoice)Gpu.Items[i]).Index==index)Gpu.SelectedIndex=i;
                cq.Value=Math.Max(cq.Minimum,Math.Min(cq.Maximum,quality));
                mode.SelectedIndex=bits>0?1:0;if(bits>0)bitrate.Value=Math.Max(bitrate.Minimum,Math.Min(bitrate.Maximum,bits/1000000));
                assume.Checked=fields[3]=="1";preview.Checked=maximum>0;
            }
            return;
        }
        if(line.StartsWith("RTXHDR_CHECKPOINT ")) {log.AppendText("재개 지점 저장: "+line.Substring(18).Trim()+" 프레임"+Environment.NewLine);return;}
        if(line.StartsWith("RTXHDR_STAGE ")) {
            SetProcessingStage(line.Substring(13).Trim());
            return;
        }
        Match m=Regex.Match(line,@"(\d+) frames(?: / ~(\d+))?, (?:recent )?([0-9.]+) fps(?:, average ([0-9.]+) fps)?(?:, ~(\d+)s remaining)?");
        if(m.Success) {
            if(processingStage>0 || cancelled) return;
            SawProgress=true;Status.Text="HDR 업스케일링 중";
            detail.Text=m.Groups[1].Value+" 프레임"+(m.Groups[2].Success?" / 약 "+m.Groups[2].Value:"");
            if(m.Groups[4].Success) detail.Text+="  ·  최근 5초 "+m.Groups[3].Value+" fps  ·  누적 평균 "+m.Groups[4].Value+" fps";
            else detail.Text+="  ·  누적 평균 "+m.Groups[3].Value+" fps";
            if(m.Groups[5].Success) detail.Text+="  ·  약 "+m.Groups[5].Value+"초 남음";
            LastProgress=detail.Text;
            double done=Double.Parse(m.Groups[1].Value,CultureInfo.InvariantCulture), total;
            if(Double.TryParse(m.Groups[2].Value,NumberStyles.None,CultureInfo.InvariantCulture,out total) && total>0) {
                progress.Style=ProgressBarStyle.Blocks;progress.Value=(int)Math.Min(990,done/total*1000);
            }
            return;
        }
        if(line.StartsWith("Preserving audio") && processingStage<2) SetProcessingStage("mux_copy");
        if(line.StartsWith("Logs: ")) {
            logDirectory=line.Substring(6).Trim();

        }
        RememberCheckpoint();
        if(log.TextLength>60000) log.Text=log.Text.Substring(log.TextLength-30000);
        if(!String.IsNullOrWhiteSpace(line)) log.AppendText(line+Environment.NewLine);
    }
    void SetProcessingStage(string stage) {
        int next; string title,message;
        switch(stage) {
            case "video_finalize":
                next=1;title="영상 프레임 처리 완료 · 인코더 마무리 중";
                message="모든 영상 프레임을 전달했습니다. 인코딩 출력을 마무리하고 있습니다.";break;
            case "mux_copy":
                next=2;title="영상 완료 · 오디오 muxing 중";
                message="오디오 원본 복사 및 영상·오디오 결합 중입니다. 오디오가 없으면 영상 컨테이너만 구성합니다.";break;
            case "mux_aac":
                next=2;title="영상 완료 · 오디오 muxing 중";
                message="오디오 AAC 변환 및 MP4 구성 중입니다. 오디오가 없으면 영상 컨테이너만 구성합니다.";break;
            case "verify":
                next=3;title="영상·오디오 처리 완료 · 결과 검사 중";
                message="출력 파일의 코덱과 HDR 색 정보를 확인하고 있습니다.";break;
            case "finalize":
                next=4;title="결과 파일 저장 마무리 중";
                message="지정한 저장 위치에 최종 파일을 저장하고 있습니다.";break;
            default:return;
        }
        if(cancelled || next<=processingStage) return;
        processingStage=next;StageHistory.Add(stage);
        Status.Text=title;detail.Text=message;
        progress.Style=next==1?ProgressBarStyle.Blocks:ProgressBarStyle.Marquee;
        if(next==1) progress.Value=1000;
        log.AppendText(title+Environment.NewLine);
    }
    void RememberCheckpoint() {
        if(!activeCheckpoint||rememberedCheckpoint||String.IsNullOrEmpty(logDirectory))return;
        string path=Path.Combine(logDirectory,"checkpoint.txt");
        if(!File.Exists(path))return;
        try {File.WriteAllText(Path.Combine(Path.GetDirectoryName(settingsPath),"last-checkpoint.txt"),path,new UTF8Encoding(false));rememberedCheckpoint=true;}
        catch(Exception e){log.AppendText("최근 작업 위치 저장 실패: "+e.Message+Environment.NewLine);}
    }
    void Complete(int code) {
        RememberCheckpoint();
        if(code!=0) SaveExitDiagnostic(code);
        Running.Dispose(); Running=null; Finished=true;
        Succeeded=code==0 && File.Exists(completedOutput);
        progress.Style=ProgressBarStyle.Blocks;progress.Value=Succeeded?1000:0;Busy(false);
        Status.Text=Succeeded?"HDR 변환 완료":cancelled?"변환을 취소했습니다":"변환에 실패했습니다";
        bool canResume=!String.IsNullOrEmpty(logDirectory)&&File.Exists(Path.Combine(logDirectory,"checkpoint.txt"));
        detail.Text=Succeeded?completedOutput:canResume?"‘이어서 변환’에서 checkpoint.txt를 선택하세요. "+logDirectory:
            !activeCheckpoint?"구간 저장을 끈 작업은 재개할 수 없습니다. 로그: "+logDirectory:"저장된 재개 지점이 없습니다. 로그: "+logDirectory;
        if(closing) Close();
    }
    static string Json(string text) {
        var value=new StringBuilder("\"");
        foreach(char c in text??"") {if(c=='\\'||c=='"')value.Append('\\').Append(c);else if(c<32)value.Append("\\u").Append(((int)c).ToString("x4"));else value.Append(c);}
        return value.Append('"').ToString();
    }
    void SaveExitDiagnostic(int code) {
        try {
            string directory=Directory.Exists(logDirectory)?logDirectory:Path.GetDirectoryName(settingsPath);
            long free=-1;try {free=new DriveInfo(Path.GetPathRoot(directory)).AvailableFreeSpace;}catch(IOException){}
            string path=Path.Combine(directory,"gui-exit-"+DateTime.UtcNow.ToString("yyyyMMdd-HHmmss")+"-"+Guid.NewGuid().ToString("N")+".json");
            File.WriteAllText(path,"{\"version\":\"0.4.1\",\"cancelled\":"+(cancelled?"true":"false")+",\"exit_code\":"+code+",\"stage\":"+processingStage+",\"last_progress\":"+Json(LastProgress)+",\"available_disk_bytes\":"+free+",\"command\":"+Json(Running.StartInfo.Arguments)+",\"log\":"+Json(log.Text)+"}",new UTF8Encoding(false));
        }catch(Exception e){log.AppendText("종료 진단 저장 실패: "+e.Message+Environment.NewLine);}
    }
    internal void CancelConversion() {
        if(Running==null) return;
        cancelled=true;cancel.Enabled=false;Status.Text="변환을 중단하는 중…";
        // The converter owns a KILL_ON_JOB_CLOSE job for FFmpeg children.
        // Killing this process closes that job too, without targeting other conversions.
        try {if(!Running.HasExited) Running.Kill();} catch(InvalidOperationException) {}
    }
    void OpenPath(string path) {
        try {if(!String.IsNullOrEmpty(path)) Process.Start(new ProcessStartInfo(path) {UseShellExecute=true});}
        catch(Exception ex) {MessageBox.Show(this,ex.Message,"열기 실패",MessageBoxButtons.OK,MessageBoxIcon.Error);}
    }
}
internal sealed class GuiSettings {
    internal int Mode=0, Format=0, GpuIndex=0;
    internal decimal Cq=18, Bitrate=40;
    internal string GpuName="";
    internal bool Checkpoint=true;
    internal static GuiSettings Load(string path) {
        GuiSettings settings=new GuiSettings();
        if(!File.Exists(path)) return settings;
        var values=new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase);
        foreach(string raw in File.ReadAllLines(path,Encoding.UTF8)) {
            string line=raw.Trim();if(line.StartsWith(";") || line.StartsWith("#")) continue;
            int equal=line.IndexOf('=');if(equal>0) values[line.Substring(0,equal).Trim()]=line.Substring(equal+1).Trim();
        }
        string value; decimal number; int index;
        if(values.TryGetValue("checkpoint",out value)) {bool enabled;if(Boolean.TryParse(value,out enabled))settings.Checkpoint=enabled;}
        if(values.TryGetValue("mode",out value) && value=="vbr") settings.Mode=1;
        if(values.TryGetValue("container",out value) && value=="mp4") settings.Format=1;
        if(values.TryGetValue("cq",out value) && Decimal.TryParse(value,NumberStyles.Number,CultureInfo.InvariantCulture,out number) && number>=0 && number<=51 && number==Decimal.Truncate(number)) settings.Cq=number;
        if(values.TryGetValue("bitrate_mbps",out value) && Decimal.TryParse(value,NumberStyles.Number,CultureInfo.InvariantCulture,out number) && number>=1 && number<=1000) settings.Bitrate=Decimal.Round(number,1);
        if(values.TryGetValue("gpu_index",out value) && Int32.TryParse(value,out index) && index>=0) settings.GpuIndex=index;
        if(values.TryGetValue("gpu_name",out value)) settings.GpuName=value;
        return settings;
    }
    internal void Save(string path) {
        string text="; RTX Video HDR - output quality settings\r\n[output]\r\nmode="+(Mode==1?"vbr":"cq")
            +"\r\ncq="+Cq.ToString(CultureInfo.InvariantCulture)
            +"\r\nbitrate_mbps="+Bitrate.ToString(CultureInfo.InvariantCulture)
            +"\r\ncontainer="+(Format==1?"mp4":"mkv")
            +"\r\ncheckpoint="+(Checkpoint?"true":"false")
            +"\r\ngpu_index="+GpuIndex.ToString(CultureInfo.InvariantCulture)
            +"\r\ngpu_name="+GpuName.Replace("\r","").Replace("\n","")+"\r\n";
        string temporary=path+"."+Guid.NewGuid().ToString("N")+".tmp";
        try {
            File.WriteAllText(temporary,text,new UTF8Encoding(false));
            if(File.Exists(path)) File.Replace(temporary,path,null);
            else File.Move(temporary,path);
        } finally {if(File.Exists(temporary)) File.Delete(temporary);}
    }
}
internal static class GuiProgram {
    [STAThread] static void Main(string[] args) {
        Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);
        HdrWindow window=new HdrWindow();if(args.Length==1) window.Input.Text=args[0];Application.Run(window);
    }
}

