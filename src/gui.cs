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
    internal readonly ComboBox Language = new ComboBox();
    readonly Dictionary<Control,Func<string>> uiText = new Dictionary<Control,Func<string>>();
    string language;
    bool applyingLanguage;
    internal string LanguageCode { get { return language; } }
    string T(string key, params object[] args) { return GuiText.Get(language,key,args); }
    void Bind(Control control,Func<string> value) {uiText[control]=value;control.Text=value();}
    void Ui(Control control,string key,params object[] args) {Bind(control,delegate {return T(key,args);});}
    void Literal(Control control,string value) {Bind(control,delegate {return value;});}
    string ErrorText(Exception error) {var local=error as GuiError;return local==null?error.Message:T(local.Key,local.Arguments);}
    void ShowError(Control control,Exception error) {Bind(control,delegate {return ErrorText(error);});}
    void ApplyLanguage() {
        applyingLanguage=true;
        try {
            language=Language.SelectedIndex==1?"ko":"en";
            foreach(var entry in uiText)entry.Key.Text=entry.Value();
            int selectedMode=mode.SelectedIndex,selectedFormat=FormatChoice.SelectedIndex;
            mode.Items[0]=T("품질 기준 (CQ)");mode.Items[1]=T("평균 비트레이트 (VBR)");
            FormatChoice.Items[0]=T("MKV · 오디오 원본 복사");FormatChoice.Items[1]=T("MP4 · 오디오 AAC 변환");
            mode.SelectedIndex=selectedMode;FormatChoice.SelectedIndex=selectedFormat;
            if(SawProgress && processingStage==0)LastProgress=detail.Text;
        } finally {applyingLanguage=false;}
        SaveSettings();
    }
    internal readonly ComboBox Gpu = new ComboBox(), FormatChoice = new ComboBox();
    sealed class GpuChoice { internal int Index; internal string Name; public override string ToString() {return "GPU " + Index + " · " + Name;} }
    readonly NumericUpDown bitrate = new NumericUpDown(), cq = new NumericUpDown();
    internal readonly CheckBox Checkpoint = new CheckBox();
    readonly Label checkpointHint = new Label();
    bool activeCheckpoint, rememberedCheckpoint, cleanupWarning;
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
        string settingsLoadError=null;
        try {savedSettings=GuiSettings.Load(settingsPath);}
        catch(Exception e) {savedSettings=new GuiSettings();settingsLoadError=e.Message;}
        language=savedSettings.Language;
        Ui(this,"RTX Video HDR 업스케일러 · SDR → HDR");
        Font = new Font("맑은 고딕", 10F);
        AutoScaleMode = AutoScaleMode.None;
        ClientSize = new Size(900, 786); MinimumSize = new Size(916, 825);
        StartPosition = FormStartPosition.CenterScreen;
        BackColor = Color.FromArgb(245, 246, 250); ForeColor = ink;
        AllowDrop = true;
        Label title = LabelAt("RTX Video HDR 업스케일러", 28, 16, 650, 38);
        title.Font = new Font(Font.FontFamily, 22F, FontStyle.Bold);
        LabelAt("Language / 언어",720,5,152,23);
        Language.DropDownStyle=ComboBoxStyle.DropDownList;Language.SetBounds(720,29,152,30);
        Language.Items.AddRange(new object[]{"English","한국어"});Language.SelectedIndex=language=="ko"?1:0;
        Language.AccessibleName="Language / 언어";Controls.Add(Language);
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
        mode.Items.AddRange(new object[] { T("품질 기준 (CQ)"), T("평균 비트레이트 (VBR)") });
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
        FormatChoice.Items.AddRange(new object[]{T("MKV · 오디오 원본 복사"),T("MP4 · 오디오 AAC 변환")});settings.Controls.Add(FormatChoice);
        FormatChoice.SelectedIndexChanged += delegate {
            if(applyingLanguage)return;
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
        Ui(preview,"시험 변환: 첫 432프레임"); preview.SetBounds(118, 145, 258, 25); settings.Controls.Add(preview);
        Ui(assume,"색 정보가 없는 SDR을 BT.709로 간주"); assume.SetBounds(392, 145, 414, 25); settings.Controls.Add(assume);

        Ui(Checkpoint,"새 변환에서 구간 저장 (재개 지원)");
        Checkpoint.SetBounds(118, 180, 335, 25);settings.Controls.Add(Checkpoint);
        checkpointHint.SetBounds(460, 181, 366, 25);settings.Controls.Add(checkpointHint);

        SetupButton(start, this, "HDR 업스케일링 시작", 28, 515, 198, delegate { StartConversion(); });
        start.BackColor = accent; start.ForeColor = Color.White; start.FlatStyle = FlatStyle.Flat; start.FlatAppearance.BorderSize = 0;
        SetupButton(cancel, this, "취소", 238, 515, 95, delegate { CancelConversion(); }); cancel.Enabled = false;
        SetupButton(resume, this, "이어서 변환", 346, 515, 165, delegate { PickResume(); });
        SetupButton(play, this, "결과 재생", 646, 515, 108, delegate { OpenPath(completedOutput); }); play.Enabled = false;
        SetupButton(folder, this, "저장 폴더", 766, 515, 106, delegate { OpenPath(Path.GetDirectoryName(completedOutput)); }); folder.Enabled = false;
        play.Anchor = folder.Anchor = AnchorStyles.Top | AnchorStyles.Right;
        Status.SetBounds(28, 565, 844, 26); Ui(Status,"변환할 영상을 선택하세요"); Status.Font = new Font(Font, FontStyle.Bold); Controls.Add(Status);
        progress.SetBounds(28, 601, 844, 10); progress.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
        progress.Maximum = 1000; Controls.Add(progress);
        detail.SetBounds(28, 624, 844, 25); Ui(detail,"하드웨어 디코딩과 GPU 색 변환을 사용합니다."); Controls.Add(detail);
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
        if(settingsLoadError!=null)log.AppendText(T("설정 파일을 읽을 수 없어 기본값으로 시작합니다: {0}",settingsLoadError)+Environment.NewLine);
        Language.SelectedIndexChanged+=delegate {ApplyLanguage();};
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
        Ui(checkpointHint,Checkpoint.Checked?"영상 약 10초마다 저장 · 추가 처리 비용":"속도 우선 · 중단한 작업은 재개 불가");
    }
    void SaveSettings() {
        if(restoringSettings || applyingLanguage) return;
        savedSettings.Language=language;
        savedSettings.Checkpoint=Checkpoint.Checked;
        savedSettings.Mode=mode.SelectedIndex;savedSettings.Cq=cq.Value;
        savedSettings.Bitrate=bitrate.Value;savedSettings.Format=FormatChoice.SelectedIndex;
        GpuChoice choice=Gpu.SelectedItem as GpuChoice;
        if(choice!=null) {savedSettings.GpuIndex=choice.Index;savedSettings.GpuName=choice.Name;}
        try {savedSettings.Save(settingsPath);settingsError="";}
        catch(Exception e) {
            if(settingsError!=e.Message) log.AppendText(T("설정 저장 실패: {0}",e.Message)+Environment.NewLine);
            settingsError=e.Message;
        }
    }
    Label LabelAt(string text, int x, int y, int w, int h) { Label l = new Label(); Ui(l,text); l.SetBounds(x,y,w,h); Controls.Add(l); return l; }
    void AddLabel(Control parent, string text, int x, int y, int w) { Label l = new Label(); Ui(l,text); l.SetBounds(x,y,w,25); parent.Controls.Add(l); }
    GroupBox Group(string text,int x,int y,int w,int h) {
        GroupBox box = new GroupBox { BackColor = Color.White }; Ui(box,text);
        box.SetBounds(x,y,w,h); box.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right; Controls.Add(box); return box;
    }
    void SetupText(TextBox t, Control parent,int x,int y,int w) {t.SetBounds(x,y,w,30);t.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right; parent.Controls.Add(t);}
    void SetupButton(Button b, Control parent,string text,int x,int y,int w,EventHandler action) {
        Ui(b,text); b.SetBounds(x,y,w,36); b.UseVisualStyleBackColor = true; b.Click += action; parent.Controls.Add(b);
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
            Ui(start,toolsFound?"GPU 처리 DLL 추가":"필수 구성 설치");
            Ui(Status,toolsFound?"FFmpeg 도구 확인 완료 · GPU 처리용 공유 DLL이 필요합니다":"처음 실행: FFmpeg 구성 설치가 필요합니다");
            Ui(detail,toolsFound?"기존 FFmpeg를 사용합니다. GPU 직접 처리를 위한 DLL만 추가로 설치하세요.":"설치 버튼을 누르면 필요한 구성을 내려받습니다. 기존 FFmpeg 도구는 유지합니다.");
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
                if(Gpu.Items.Count==0) throw new GuiError("사용 가능한 NVIDIA GPU가 없습니다.");
                Gpu.SelectedIndex=0;
            }
        } catch(Exception e) {start.Enabled=false;Ui(Status,"GPU를 확인할 수 없습니다");ShowError(detail,e);}
    }
    static string Clean(string text) { return text.Trim().Trim('"'); }
    void PickInput() {using(OpenFileDialog d = new OpenFileDialog {Filter = T("영상 파일|*.mp4;*.mkv;*.mov;*.ts|모든 파일|*.*"), Title = T("SDR 영상 선택")}) if(d.ShowDialog(this)==DialogResult.OK) Input.Text=d.FileName;}
    void PickOutput() {using(SaveFileDialog d = new SaveFileDialog {Filter=T(FormatChoice.SelectedIndex==1?"MP4 HDR 영상|*.mp4":"MKV HDR 영상|*.mkv"),Title=T("저장 위치 선택"),DefaultExt=FormatChoice.SelectedIndex==1?"mp4":"mkv",FileName=Path.GetFileName(Clean(Output.Text)),OverwritePrompt=true}) if(d.ShowDialog(this)==DialogResult.OK) Output.Text=d.FileName;}
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
            picker.Title=T("이어서 변환할 작업의 checkpoint.txt 선택");
            picker.Filter=T("HDR 작업 체크포인트|checkpoint.txt");
            string recent=Path.Combine(Path.GetDirectoryName(settingsPath),"last-checkpoint.txt");
            try {if(File.Exists(recent)) {string path=File.ReadAllText(recent,Encoding.UTF8).Trim();if(File.Exists(path)){picker.InitialDirectory=Path.GetDirectoryName(path);picker.FileName=path;}}}catch(IOException){}
            if(picker.ShowDialog(this)==DialogResult.OK) StartConversion(picker.FileName);
        }
    }
    internal void StartConversion(string checkpoint = null) {
        if(MissingRuntime()) {
            try {Process.Start(new ProcessStartInfo(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"Setup-Runtime.cmd")){UseShellExecute=true});Close();}
            catch(Exception e) {Ui(Status,"구성 설치를 시작하지 못했습니다");ShowError(detail,e);}
            return;
        }
        if(Running != null) return;
        try {
            string engine=Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"RTXVideoHDRConvert.exe");
            if(!File.Exists(engine)) throw new GuiError("같은 폴더에 RTXVideoHDRConvert.exe가 필요합니다.");
            string output="",arguments="";
            if(checkpoint!=null) {
                if(!File.Exists(checkpoint))throw new GuiError("체크포인트를 찾을 수 없습니다.");
                arguments="--resume "+Quote(Path.GetFullPath(checkpoint));
            } else {
            string input=Path.GetFullPath(Clean(Input.Text));output=Path.GetFullPath(Clean(Output.Text));
            if(!File.Exists(input)) throw new GuiError("원본 영상 파일을 찾을 수 없습니다.");
            string extension=FormatChoice.SelectedIndex==1?".mp4":".mkv";
            if(!output.EndsWith(extension,StringComparison.OrdinalIgnoreCase)) throw new GuiError("저장 파일 확장자를 {0}로 지정하세요.",extension);
            GpuChoice selected=Gpu.SelectedItem as GpuChoice;
            if(selected==null) throw new GuiError("사용 가능한 NVIDIA GPU를 선택하세요.");
            if(File.Exists(output)) throw new GuiError("저장 위치에 파일이 이미 있습니다. 다른 이름을 지정하세요.");
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
            activeCheckpoint=checkpoint!=null||Checkpoint.Checked;rememberedCheckpoint=cleanupWarning=false;
            Running=p; Finished=Succeeded=SawProgress=cancelled=false; completedOutput=output; logDirectory="";
            processingStage=0;StageHistory.Clear();LastProgress="";
            log.Clear(); Busy(true); progress.Style=ProgressBarStyle.Marquee; Ui(Status,"입력 확인 및 HDR 준비 중…");
            Ui(detail,"전체 영상을 사전 디코딩하지 않고 변환 중에 검사합니다.");
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
            Ui(Status,"시작할 수 없습니다"); ShowError(detail,ex); log.AppendText(ErrorText(ex)+Environment.NewLine);
        }
    }
    void Dispatch(Action action) { if(!IsDisposed && IsHandleCreated) {try {BeginInvoke(action);} catch(InvalidOperationException) {}} }
    void Receive(string line) {
        if(line.StartsWith("RTXHDR_CLEANUP_WARNING ")) {cleanupWarning=true;log.AppendText(T("결과 영상은 완료됐지만 일부 중간 파일 정리가 남았습니다. cleanup.json을 확인하세요.")+Environment.NewLine);return;}
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
        if(line.StartsWith("RTXHDR_CHECKPOINT ")) {log.AppendText(T("재개 지점 저장: {0} 프레임",line.Substring(18).Trim())+Environment.NewLine);return;}
        if(line.StartsWith("RTXHDR_STAGE ")) {
            SetProcessingStage(line.Substring(13).Trim());
            return;
        }
        Match m=Regex.Match(line,@"(\d+) frames(?: / ~(\d+))?, (?:recent )?([0-9.]+) fps(?:, average ([0-9.]+) fps)?(?:, ~(\d+)s remaining)?");
        if(m.Success) {
            if(processingStage>0 || cancelled) return;
            SawProgress=true;Ui(Status,"HDR 업스케일링 중");
            Bind(detail,delegate {
                string text=T("{0} 프레임",m.Groups[1].Value)+(m.Groups[2].Success?T(" / 약 {0}",m.Groups[2].Value):"");
                text+=m.Groups[4].Success?T("  ·  최근 5초 {0} fps  ·  누적 평균 {1} fps",m.Groups[3].Value,m.Groups[4].Value):T("  ·  누적 평균 {0} fps",m.Groups[3].Value);
                if(m.Groups[5].Success)text+=T("  ·  약 {0}초 남음",m.Groups[5].Value);
                return text;
            });
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
            case "cleanup":
                next=5;title="결과 저장 완료 · 중간 파일 정리 중";
                message="완료된 결과를 보존하고 큰 중간 영상 파일을 삭제합니다. 로그는 남깁니다.";break;
            default:return;
        }
        if(cancelled || next<=processingStage) return;
        processingStage=next;StageHistory.Add(stage);
        if(next==5)cancel.Enabled=false;
        Ui(Status,title);Ui(detail,message);
        progress.Style=next==1?ProgressBarStyle.Blocks:ProgressBarStyle.Marquee;
        if(next==1) progress.Value=1000;
        log.AppendText(T(title)+Environment.NewLine);
    }
    void RememberCheckpoint() {
        if(!activeCheckpoint||rememberedCheckpoint||String.IsNullOrEmpty(logDirectory))return;
        string path=Path.Combine(logDirectory,"checkpoint.txt");
        if(!File.Exists(path))return;
        try {File.WriteAllText(Path.Combine(Path.GetDirectoryName(settingsPath),"last-checkpoint.txt"),path,new UTF8Encoding(false));rememberedCheckpoint=true;}
        catch(Exception e){log.AppendText(T("최근 작업 위치 저장 실패: {0}",e.Message)+Environment.NewLine);}
    }
    void Complete(int code) {
        RememberCheckpoint();
        if(code!=0) SaveExitDiagnostic(code);
        Running.Dispose(); Running=null; Finished=true;
        Succeeded=code==0 && File.Exists(completedOutput);
        progress.Style=ProgressBarStyle.Blocks;progress.Value=Succeeded?1000:0;Busy(false);
        Ui(Status,Succeeded?(cleanupWarning?"HDR 변환 완료 · 중간 파일 정리 일부 남음":"HDR 변환 완료"):cancelled?"변환을 취소했습니다":"변환에 실패했습니다");
        bool canResume=!String.IsNullOrEmpty(logDirectory)&&File.Exists(Path.Combine(logDirectory,"checkpoint.txt"));
        if(Succeeded) {
            if(cleanupWarning)Ui(detail,"결과 영상: {0} · 정리 내역은 cleanup.json 참고",completedOutput);
            else Literal(detail,completedOutput);
        } else Ui(detail,canResume?"‘이어서 변환’에서 checkpoint.txt를 선택하세요. {0}":
            !activeCheckpoint?"구간 저장을 끈 작업은 재개할 수 없습니다. 로그: {0}":"저장된 재개 지점이 없습니다. 로그: {0}",logDirectory);
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
            File.WriteAllText(path,"{\"version\":\"0.4.2\",\"language\":"+Json(language)+",\"cancelled\":"+(cancelled?"true":"false")+",\"exit_code\":"+code+",\"stage\":"+processingStage+",\"last_progress\":"+Json(LastProgress)+",\"available_disk_bytes\":"+free+",\"command\":"+Json(Running.StartInfo.Arguments)+",\"log\":"+Json(log.Text)+"}",new UTF8Encoding(false));
        }catch(Exception e){log.AppendText(T("종료 진단 저장 실패: {0}",e.Message)+Environment.NewLine);}
    }
    internal void CancelConversion() {
        if(Running==null) return;
        if(processingStage>=5) {cancel.Enabled=false;Ui(Status,"결과 저장 완료 · 중간 파일 정리 중");return;}
        cancelled=true;cancel.Enabled=false;Ui(Status,"변환을 중단하는 중…");
        // The converter owns a KILL_ON_JOB_CLOSE job for FFmpeg children.
        // Killing this process closes that job too, without targeting other conversions.
        try {if(!Running.HasExited) Running.Kill();} catch(InvalidOperationException) {}
    }
    void OpenPath(string path) {
        try {if(!String.IsNullOrEmpty(path)) Process.Start(new ProcessStartInfo(path) {UseShellExecute=true});}
        catch(Exception ex) {MessageBox.Show(this,ErrorText(ex),T("열기 실패"),MessageBoxButtons.OK,MessageBoxIcon.Error);}
    }
}
internal sealed class GuiError : Exception {
    internal readonly string Key;
    internal readonly object[] Arguments;
    internal GuiError(string key,params object[] args):base(key) {Key=key;Arguments=args;}
}
internal static class GuiText {
    // Korean source keys and English translations share formatting arguments.
    internal static readonly Dictionary<string,string> English = new Dictionary<string,string> {
        {"RTX Video HDR 업스케일러 · SDR → HDR","RTX Video HDR Upscaler · SDR → HDR"},
        {"RTX Video HDR 업스케일러","RTX Video HDR Upscaler"},
        {"NVIDIA RTX Video HDR로 SDR 영상을 HDR로 업스케일링합니다.","Upscale SDR video to HDR with NVIDIA RTX Video HDR."},
        {"원본 해상도·프레임률 유지  ·  HEVC 10비트  ·  MKV / MP4 저장","Preserves resolution and frame rate  ·  HEVC 10-bit  ·  MKV / MP4 output"},
        {"01   영상 파일","01   Video files"},
        {"원본 영상","Source video"},
        {"찾아보기","Browse"},
        {"저장 위치","Output path"},
        {"변경","Change"},
        {"영상을 바꾸면 새 원본 옆의 HDR 파일명으로 저장 위치가 자동 변경됩니다.","Changing the source sets a new HDR output filename beside it."},
        {"02   출력 품질 · 재개 설정","02   Output quality · Resume settings"},
        {"인코딩 방식","Encoding"},
        {"품질 기준 (CQ)","Quality-based (CQ)"},
        {"평균 비트레이트 (VBR)","Average bitrate (VBR)"},
        {"사용할 GPU","GPU"},
        {"저장 형식","Format"},
        {"MKV · 오디오 원본 복사","MKV · Copy original audio"},
        {"MP4 · 오디오 AAC 변환","MP4 · Convert audio to AAC"},
        {"CQ는 낮을수록 높은 품질을 지향합니다. VBR은 입력한 평균 비트레이트를 목표로 합니다.","Lower CQ targets higher quality. VBR targets the specified average bitrate."},
        {"시험 변환: 첫 432프레임","Preview: first 432 frames"},
        {"색 정보가 없는 SDR을 BT.709로 간주","Assume BT.709 for untagged SDR"},
        {"새 변환에서 구간 저장 (재개 지원)","Save segments for new jobs (resume)"},
        {"HDR 업스케일링 시작","Start HDR upscaling"},
        {"취소","Cancel"},
        {"이어서 변환","Resume conversion"},
        {"결과 재생","Play result"},
        {"저장 폴더","Open folder"},
        {"변환할 영상을 선택하세요","Select a video to convert"},
        {"하드웨어 디코딩과 GPU 색 변환을 사용합니다.","Uses hardware decoding and GPU color conversion."},
        {"설정 파일을 읽을 수 없어 기본값으로 시작합니다: {0}","Cannot read settings; using defaults: {0}"},
        {"영상 약 10초마다 저장 · 추가 처리 비용","Every ~10s of video · extra overhead"},
        {"속도 우선 · 중단한 작업은 재개 불가","Speed priority · interrupted jobs cannot resume"},
        {"설정 저장 실패: {0}","Could not save settings: {0}"},
        {"GPU 처리 DLL 추가","Add GPU DLLs"},
        {"필수 구성 설치","Install runtime"},
        {"FFmpeg 도구 확인 완료 · GPU 처리용 공유 DLL이 필요합니다","FFmpeg found · GPU shared DLLs are required"},
        {"처음 실행: FFmpeg 구성 설치가 필요합니다","First launch: FFmpeg runtime is required"},
        {"기존 FFmpeg를 사용합니다. GPU 직접 처리를 위한 DLL만 추가로 설치하세요.","Your existing FFmpeg will be used. Install the DLLs needed for direct GPU processing."},
        {"설치 버튼을 누르면 필요한 구성을 내려받습니다. 기존 FFmpeg 도구는 유지합니다.","Click Install runtime to download required components. Existing FFmpeg tools are kept."},
        {"사용 가능한 NVIDIA GPU가 없습니다.","No NVIDIA GPU is available."},
        {"GPU를 확인할 수 없습니다","Could not detect GPUs"},
        {"영상 파일|*.mp4;*.mkv;*.mov;*.ts|모든 파일|*.*","Video files|*.mp4;*.mkv;*.mov;*.ts|All files|*.*"},
        {"SDR 영상 선택","Select an SDR video"},
        {"저장 위치 선택","Choose output location"},
        {"MP4 HDR 영상|*.mp4","MP4 HDR video|*.mp4"},
        {"MKV HDR 영상|*.mkv","MKV HDR video|*.mkv"},
        {"이어서 변환할 작업의 checkpoint.txt 선택","Select checkpoint.txt to resume a job"},
        {"HDR 작업 체크포인트|checkpoint.txt","HDR job checkpoint|checkpoint.txt"},
        {"구성 설치를 시작하지 못했습니다","Could not start runtime installation"},
        {"같은 폴더에 RTXVideoHDRConvert.exe가 필요합니다.","RTXVideoHDRConvert.exe must be in the same folder."},
        {"체크포인트를 찾을 수 없습니다.","Checkpoint file not found."},
        {"원본 영상 파일을 찾을 수 없습니다.","Source video file not found."},
        {"저장 파일 확장자를 {0}로 지정하세요.","Use {0} as the output file extension."},
        {"사용 가능한 NVIDIA GPU를 선택하세요.","Select an available NVIDIA GPU."},
        {"저장 위치에 파일이 이미 있습니다. 다른 이름을 지정하세요.","The output file already exists. Choose a different name."},
        {"입력 확인 및 HDR 준비 중…","Checking input and preparing HDR…"},
        {"전체 영상을 사전 디코딩하지 않고 변환 중에 검사합니다.","Frames are checked during conversion, without pre-decoding the entire video."},
        {"시작할 수 없습니다","Could not start conversion"},
        {"결과 영상은 완료됐지만 일부 중간 파일 정리가 남았습니다. cleanup.json을 확인하세요.","Output is complete, but some intermediate files remain. See cleanup.json."},
        {"재개 지점 저장: {0} 프레임","Checkpoint saved: {0} frames"},
        {"HDR 업스케일링 중","HDR upscaling in progress"},
        {"{0} 프레임","{0} frames"},
        {" / 약 {0}"," / ~{0}"},
        {"  ·  최근 5초 {0} fps  ·  누적 평균 {1} fps","  ·  Recent 5s {0} fps  ·  Average {1} fps"},
        {"  ·  누적 평균 {0} fps","  ·  Average {0} fps"},
        {"  ·  약 {0}초 남음","  ·  ~{0}s remaining"},
        {"영상 프레임 처리 완료 · 인코더 마무리 중","Video frames complete · Finalizing encoder"},
        {"모든 영상 프레임을 전달했습니다. 인코딩 출력을 마무리하고 있습니다.","All video frames have been submitted. Finishing encoder output."},
        {"영상 완료 · 오디오 muxing 중","Video complete · Muxing audio"},
        {"오디오 원본 복사 및 영상·오디오 결합 중입니다. 오디오가 없으면 영상 컨테이너만 구성합니다.","Copying original audio and muxing the file. Video-only inputs create a video container."},
        {"오디오 AAC 변환 및 MP4 구성 중입니다. 오디오가 없으면 영상 컨테이너만 구성합니다.","Converting audio to AAC and creating MP4. Video-only inputs create a video container."},
        {"영상·오디오 처리 완료 · 결과 검사 중","Video and audio complete · Checking output"},
        {"출력 파일의 코덱과 HDR 색 정보를 확인하고 있습니다.","Checking output codecs and HDR color metadata."},
        {"결과 파일 저장 마무리 중","Finalizing output file"},
        {"지정한 저장 위치에 최종 파일을 저장하고 있습니다.","Saving the final file at the selected output location."},
        {"결과 저장 완료 · 중간 파일 정리 중","Output saved · Cleaning intermediate files"},
        {"완료된 결과를 보존하고 큰 중간 영상 파일을 삭제합니다. 로그는 남깁니다.","Keeping the completed output and logs while deleting large intermediate videos."},
        {"최근 작업 위치 저장 실패: {0}","Could not save the latest job location: {0}"},
        {"HDR 변환 완료 · 중간 파일 정리 일부 남음","HDR conversion complete · Some cleanup remains"},
        {"HDR 변환 완료","HDR conversion complete"},
        {"변환을 취소했습니다","Conversion canceled"},
        {"변환에 실패했습니다","Conversion failed"},
        {"결과 영상: {0} · 정리 내역은 cleanup.json 참고","Output: {0} · See cleanup.json for cleanup details"},
        {"‘이어서 변환’에서 checkpoint.txt를 선택하세요. {0}","Use Resume conversion and select checkpoint.txt. {0}"},
        {"구간 저장을 끈 작업은 재개할 수 없습니다. 로그: {0}","Jobs with checkpoints disabled cannot resume. Logs: {0}"},
        {"저장된 재개 지점이 없습니다. 로그: {0}","No saved checkpoint is available. Logs: {0}"},
        {"종료 진단 저장 실패: {0}","Could not save exit diagnostics: {0}"},
        {"변환을 중단하는 중…","Stopping conversion…"},
        {"열기 실패","Could not open"}
    };
    internal static string Get(string language,string key,params object[] args) {
        string value;
        if(language!="ko" && English.TryGetValue(key,out value))key=value;
        return args.Length==0?key:String.Format(CultureInfo.InvariantCulture,key,args);
    }
}
internal sealed class GuiSettings {
    internal int Mode=0, Format=0, GpuIndex=0;
    internal decimal Cq=18, Bitrate=40;
    internal string GpuName="";
    internal string Language=DefaultLanguage;
    internal static string DefaultLanguage {get {return CultureInfo.CurrentUICulture.TwoLetterISOLanguageName=="ko"?"ko":"en";}}
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
        if(values.TryGetValue("language",out value) && (value=="ko" || value=="en"))settings.Language=value;
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
        string text="; RTX Video HDR - interface and output settings\r\n[interface]\r\nlanguage="+(Language=="ko"?"ko":"en")+"\r\n\r\n[output]\r\nmode="+(Mode==1?"vbr":"cq")
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
