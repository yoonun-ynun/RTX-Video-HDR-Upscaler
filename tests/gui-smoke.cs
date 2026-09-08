using System;
using System.Drawing;
using System.IO;
using System.Windows.Forms;
internal static class GuiSmokeTest {
    [STAThread] static int Main(string[] args) {
        Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);
        HdrWindow f=new HdrWindow(args[2]+".ini");f.Opacity=0;f.ShowInTaskbar=false;
        bool english=args.Length>4 && args[4]=="en";
        f.Language.SelectedIndex=english?0:1;
        bool cancel=args.Length>3 && args[3].EndsWith("cancel");
        bool compare=args.Length>3 && args[3].StartsWith("compare");
        bool fast=(args.Length>3 && args[3].StartsWith("fast")), routing=false;
        string recent=Path.Combine(Path.GetDirectoryName(args[2]),"last-checkpoint.txt");
        string previousRecent=File.Exists(recent)?File.ReadAllText(recent):null;
        DateTime deadline=DateTime.Now.AddSeconds(90);
        int exit=1;Timer timer=new Timer {Interval=100};
        f.Shown += delegate {
            f.FormatChoice.SelectedIndex=args[1].EndsWith(".mp4")?1:0;
            f.mode.SelectedIndex=args.Length>3 && args[3]=="vbr"?1:0;
            f.Checkpoint.Checked=compare || !fast;
            f.preview.Checked=compare && args[3]!="compare-full";f.Comparison.Checked=compare;
            f.Input.Text=args[0];f.Output.Text=args[1];
            if(f.Gpu.Items.Count>1 && args[1].EndsWith(".mp4")) f.Gpu.SelectedIndex=1;
            using(Bitmap bitmap=new Bitmap(f.Width,f.Height)) {f.DrawToBitmap(bitmap,new Rectangle(0,0,f.Width,f.Height));bitmap.Save(args[2]);}
            f.StartConversion();routing=f.Running!=null && f.Running.StartInfo.Arguments.Contains("--no-checkpoint")==fast && !f.Checkpoint.Enabled && f.Running.StartInfo.Arguments.Contains("--compare-sdr-hdr")==compare && !f.Comparison.Enabled;timer.Start();
        };
        timer.Tick += delegate {
            if(cancel && f.SawProgress && f.Running!=null) f.CancelConversion();
            if(f.Finished) {
                bool rates=f.LastProgress.Contains(english?"Recent 5s":"최근 5초") && f.LastProgress.Contains(english?"Average":"누적 평균");
                string expected="video_finalize,"+(args[1].EndsWith(".mp4")?"mux_aac":"mux_copy")+",verify,finalize,cleanup";
                bool stages=String.Join(",",f.StageHistory)==expected;
                exit=cancel?(!f.Succeeded && !File.Exists(args[1]) && f.SawProgress && rates?0:1):(f.Succeeded && f.SawProgress && rates && stages?0:1);
                if(!routing)exit=1;
                if(fast) {
                    string now=File.Exists(recent)?File.ReadAllText(recent):null;
                    if(now!=previousRecent||f.Diagnostics.Contains(english?"Checkpoint saved":"재개 지점 저장"))exit=1;
                    if(cancel&&!f.Diagnostics.Contains(english?"Jobs with checkpoints disabled cannot resume":"구간 저장을 끈 작업은 재개할 수 없습니다"))exit=1;
                }
                File.WriteAllText(args[2]+".txt", "exit="+exit+" status="+f.Status.Text+" progress="+f.SawProgress+"\nstages="+String.Join(",",f.StageHistory)+"\n"+f.LastProgress+"\n"+f.Diagnostics);
                timer.Stop();f.Close();
            } else if(DateTime.Now>deadline) {timer.Stop();f.CancelConversion();f.Close();}
        };
        Application.Run(f);return exit;
    }
}
