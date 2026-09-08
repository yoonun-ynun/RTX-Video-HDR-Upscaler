using System;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Text.RegularExpressions;
using System.Threading;
using System.Windows.Forms;

internal static class GuiLanguageTest {
    static readonly BindingFlags Private=BindingFlags.Instance|BindingFlags.NonPublic;
    static void Require(bool value,string reason) {if(!value)throw new Exception(reason);}
    static void Receive(HdrWindow f,string line) {typeof(HdrWindow).GetMethod("Receive",Private).Invoke(f,new object[]{line});}
    static void CheckEnglish(Control parent) {
        foreach(Control c in parent.Controls) {
            if(!(c is TextBox) && c.Text!="Language / 언어")Require(!Regex.IsMatch(c.Text,"[가-힣]"),"Untranslated control: "+c.Text);
            CheckEnglish(c);
        }
    }
    static void Capture(HdrWindow f,string path) {
        f.Refresh();using(var image=new Bitmap(f.Width,f.Height)){f.DrawToBitmap(image,new Rectangle(0,0,f.Width,f.Height));image.Save(path);}
    }
    [STAThread] static int Main(string[] args) {
        Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);
        string path=args[0],operation=args[1];int result=1;
        if(operation=="defaults") {
            var culture=Thread.CurrentThread.CurrentUICulture;
            try {
                File.WriteAllText(path,"[output]\ncq=22\n[interface]\nlanguage=invalid\n");
                Thread.CurrentThread.CurrentUICulture=new CultureInfo("en-US");
                Require(GuiSettings.Load(path).Language=="en","Invalid language must use OS default");
                Thread.CurrentThread.CurrentUICulture=new CultureInfo("ko-KR");
                Require(GuiSettings.Load(path).Language=="ko" && GuiSettings.Load(path).Cq==22,"Korean default with existing quality settings");
                Thread.CurrentThread.CurrentUICulture=new CultureInfo("ja-JP");
                Require(new GuiSettings().Language=="en","Non-Korean Windows defaults to English");
                File.WriteAllText(path,"[output]\nmode=vbr\ncq=22\nbitrate_mbps=75.5\n");
                Require(GuiSettings.Load(path).Language=="en" && GuiSettings.Load(path).Mode==1,"Legacy settings remain compatible");
                File.WriteAllText(path+".defaults.txt","PASS defaults and legacy settings");return 0;
            } finally {Thread.CurrentThread.CurrentUICulture=culture;}
        }
        using(var f=new HdrWindow(path)) {
            f.Opacity=0;f.ShowInTaskbar=false;
            f.Shown+=delegate {
                try {
                    if(operation.StartsWith("write-")) {
                        f.Language.SelectedIndex=operation.EndsWith("ko")?1:0;
                        f.mode.SelectedIndex=1;f.FormatChoice.SelectedIndex=1;f.Checkpoint.Checked=false;
                        Require(GuiSettings.Load(path).Language==f.LanguageCode,"Language must save immediately");
                    } else if(operation.StartsWith("read-")) {
                        string expected=operation.EndsWith("ko")?"ko":"en";
                        Require(f.LanguageCode==expected && f.Text.Contains(expected=="ko"?"업스케일러":"Upscaler"),"Language must survive process restart");
                        Require(f.mode.SelectedIndex==1 && f.FormatChoice.SelectedIndex==1 && !f.Checkpoint.Checked,"Language persistence must preserve quality settings");
                    } else if(operation=="runtime") {
                        f.Language.SelectedIndex=0;CheckEnglish(f);
                        bool missing=(bool)typeof(HdrWindow).GetMethod("MissingRuntime",Private).Invoke(f,null);
                        if(missing)Require(f.Status.Text.Contains("required"),"Missing runtime guidance in English");
                        Capture(f,path+".runtime-en.png");
                        f.Language.SelectedIndex=1;Require(Regex.IsMatch(f.Status.Text,"[가-힣]"),"Runtime/ready state switches to Korean");
                    } else {
                        foreach(var entry in GuiText.English) {
                            GuiText.Get("en",entry.Key,"0","1");GuiText.Get("ko",entry.Key,"0","1");
                            Require(String.Join(",",Regex.Matches(entry.Key,@"\{\d+\}").ToStrings())==String.Join(",",Regex.Matches(entry.Value,@"\{\d+\}").ToStrings()),"Translation arguments: "+entry.Key);
                        }
                        f.Language.SelectedIndex=0;
                        f.Input.Text=Path.Combine(Path.GetDirectoryName(path),"comparison-source.mp4");
                        f.Checkpoint.Checked=true;
                        Require(f.Comparison.Enabled && !f.Comparison.Checked,"Comparison defaults off, available for full video");
                        f.preview.Checked=true;f.Comparison.Checked=true;
                        Require(f.Comparison.Enabled && f.Checkpoint.Enabled && f.Checkpoint.Checked,"Comparison supports checkpoints");
                        Require(f.Output.Text.EndsWith(".compare.hdr.mp4"),"Comparison default filename");
                        f.FormatChoice.SelectedIndex=0;Require(f.Output.Text.EndsWith(".compare.hdr.mkv"),"Comparison MKV name");
                        f.Language.SelectedIndex=1;Require(f.Output.Text.EndsWith(".compare.hdr.mkv") && f.Comparison.Checked,"Language preserves comparison");
                        f.Language.SelectedIndex=0;
                        CheckEnglish(f);Capture(f,path+".compare-en.png");
                        f.Language.SelectedIndex=1;Capture(f,path+".compare-ko.png");f.Language.SelectedIndex=0;
                        f.preview.Checked=false;Require(f.Comparison.Enabled && f.Checkpoint.Enabled && f.Output.Text.Contains(".compare."),"Full conversion retains comparison and checkpoints");
                        f.preview.Checked=true;Require(f.Output.Text.Contains(".compare."),"Preview toggle restores comparison name");
                        f.Output.Text=Path.Combine(Path.GetDirectoryName(path),"manual.mkv");
                        f.Comparison.Checked=false;Require(f.Output.Text.EndsWith("manual.mkv"),"Comparison toggle preserves custom output");
                        f.Comparison.Checked=true;f.Input.Text=Path.Combine(Path.GetDirectoryName(path),"another.mp4");
                        Require(f.Output.Text.EndsWith("another.compare.hdr.mkv"),"Source change uses comparison name");
                        f.preview.Checked=false;f.Comparison.Checked=false;
                        CheckEnglish(f);Capture(f,path+".en.png");
                        f.Language.SelectedIndex=1;Require(f.Text.Contains("업스케일러"),"Korean title");Capture(f,path+".ko.png");
                        f.Input.Text=Path.Combine(Path.GetDirectoryName(path),"missing-{0}.mp4");
                        f.FormatChoice.SelectedIndex=1;f.mode.SelectedIndex=1;f.Checkpoint.Checked=false;
                        string output=Path.Combine(Path.GetDirectoryName(path),"custom-{output}.mp4");f.Output.Text=output;
                        f.StartConversion();Require(f.Running==null && f.Diagnostics.Contains("원본 영상 파일을 찾을 수 없습니다"),"Korean input error");
                        f.Language.SelectedIndex=0;
                        Require(f.Diagnostics.Contains("Source video file not found."),"Existing error must switch language");
                        // Simulate a live process without starting/killing any user process.
                        using(var pending=new System.Diagnostics.Process()) {
                            f.Running=pending;
                            typeof(HdrWindow).GetMethod("Busy",Private).Invoke(f,new object[]{true});
                            try {
                                Receive(f,"120 frames / ~240, recent 40.0 fps, average 35.0 fps, ~3s remaining");
                                Require(f.LastProgress.Contains("Recent 5s") && f.LastProgress.Contains("Average"),"English progress");
                                f.Language.SelectedIndex=1;Require(f.Diagnostics.Contains("최근 5초"),"Live progress translated immediately");
                                f.Language.SelectedIndex=0;
                                Require(f.Running==pending && f.Output.Text==output && f.mode.SelectedIndex==1 && f.FormatChoice.SelectedIndex==1 && !f.Checkpoint.Checked,"Language switch must not mutate job or settings");
                                Require(f.Language.Enabled && !f.Input.Enabled,"Language remains usable during conversion");
                                foreach(string stage in new[]{"video_finalize","mux_aac","verify","finalize","cleanup"}) {
                                    Receive(f,"RTXHDR_STAGE "+stage);
                                    int count=f.StageHistory.Count;
                                    f.Language.SelectedIndex=1;Require(Regex.IsMatch(f.Status.Text,"[가-힣]"),"Korean stage: "+stage);
                                    f.Language.SelectedIndex=0;Require(!Regex.IsMatch(f.Status.Text,"[가-힣]"),"English stage: "+stage);
                                    Require(f.StageHistory.Count==count,"Translation must not replay stage events");
                                }
                                Require(f.Status.Text.Contains("Cleaning intermediate"),"English cleanup");
                                Capture(f,path+".cleanup-en.png");
                            } finally {f.Running=null;}
                        }
                    }
                    File.WriteAllText(path+"."+operation+".txt","PASS "+operation);result=0;
                } catch(Exception e) {File.WriteAllText(path+"."+operation+".txt",e.ToString());}
                f.Close();
            };
            Application.Run(f);
        }
        return result;
    }
    static string[] ToStrings(this MatchCollection matches) {var result=new string[matches.Count];for(int i=0;i<matches.Count;i++)result[i]=matches[i].Value;return result;}
}
