using System;
using System.Drawing;
using System.IO;
using System.Windows.Forms;
internal static class GuiSmokeTest {
    [STAThread] static int Main(string[] args) {
        Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);
        HdrWindow f=new HdrWindow(args[2]+".ini");f.Opacity=0;f.ShowInTaskbar=false;
        bool cancel=args.Length>3 && args[3]=="cancel";
        DateTime deadline=DateTime.Now.AddSeconds(90);
        int exit=1;Timer timer=new Timer {Interval=100};
        f.Shown += delegate {
            f.FormatChoice.SelectedIndex=args[1].EndsWith(".mp4")?1:0;
            f.mode.SelectedIndex=args.Length>3 && args[3]=="vbr"?1:0;
            f.Input.Text=args[0];f.Output.Text=args[1];
            if(f.Gpu.Items.Count>1 && args[1].EndsWith(".mp4")) f.Gpu.SelectedIndex=1;
            using(Bitmap bitmap=new Bitmap(f.Width,f.Height)) {f.DrawToBitmap(bitmap,new Rectangle(0,0,f.Width,f.Height));bitmap.Save(args[2]);}
            f.StartConversion();timer.Start();
        };
        timer.Tick += delegate {
            if(cancel && f.SawProgress && f.Running!=null) f.CancelConversion();
            if(f.Finished) {
                bool rates=f.LastProgress.Contains("최근 5초") && f.LastProgress.Contains("누적 평균");
                exit=cancel?(!f.Succeeded && !File.Exists(args[1]) && f.SawProgress && rates?0:1):(f.Succeeded && f.SawProgress && rates?0:1);
                File.WriteAllText(args[2]+".txt", "exit="+exit+" status="+f.Status.Text+" progress="+f.SawProgress+"\n"+f.LastProgress+"\n"+f.Diagnostics);
                timer.Stop();f.Close();
            } else if(DateTime.Now>deadline) {timer.Stop();f.CancelConversion();f.Close();}
        };
        Application.Run(f);return exit;
    }
}
