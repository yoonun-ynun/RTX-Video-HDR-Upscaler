using System;
using System.IO;
using System.Windows.Forms;

// Two independent windows/settings loads: cancel, close, reopen, explicitly resume.
internal static class GuiResumeTest {
    [STAThread] static int Main(string[] args) {
        Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);
        string settings=args[1]+".ini",checkpoint=null;bool passed=false;
        for(int attempt=0;attempt<2;attempt++) {
            bool restarting=attempt==1,requested=false;
            HdrWindow f=new HdrWindow(settings);f.Opacity=0;f.ShowInTaskbar=false;
            Timer timer=new Timer {Interval=20};DateTime deadline=DateTime.UtcNow.AddMinutes(3);
            f.Shown+=delegate {
                if(restarting){f.Checkpoint.Checked=false;f.StartConversion(checkpoint);}
                else {f.Input.Text=args[0];f.Output.Text=args[1];if(f.Gpu.Items.Count>1)f.Gpu.SelectedIndex=1;f.StartConversion();}
                timer.Start();
            };
            timer.Tick+=delegate {
                if(!restarting&&!requested&&f.Diagnostics.Contains("재개 지점 저장")) {requested=true;f.CancelConversion();}
                if(f.Finished) {
                    if(!restarting) {
                        checkpoint=File.ReadAllText(Path.Combine(Path.GetDirectoryName(settings),"last-checkpoint.txt")).Trim();
                        passed=!f.Succeeded&&File.Exists(checkpoint)&&Directory.GetFiles(Path.GetDirectoryName(checkpoint),"gui-exit-*.json").Length>0;
                    } else passed=passed&&f.Succeeded&&!f.Checkpoint.Checked&&!GuiSettings.Load(settings).Checkpoint&&f.Input.Text==args[0]&&f.Output.Text==args[1];
                    File.AppendAllText(args[1]+".test.txt", "attempt="+attempt+" passed="+passed+"\n"+f.Diagnostics+"\n");
                    timer.Stop();f.Close();
                } else if(DateTime.UtcNow>deadline) {passed=false;timer.Stop();f.CancelConversion();f.Close();}
            };
            Application.Run(f);timer.Dispose();
            if(!passed)break;
        }
        return passed?0:1;
    }
}
