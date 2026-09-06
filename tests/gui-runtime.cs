using System;
using System.Drawing;
using System.IO;
using System.Windows.Forms;
internal static class GuiRuntimeTest {
    [STAThread] static int Main(string[] args) {
        Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);
        int result=1;
        using(HdrWindow f=new HdrWindow(args[0]+".ini")) {
            f.Opacity=0;f.ShowInTaskbar=false;
            f.Shown+=delegate {
                try {
                    if(args.Length>1 && args[1]=="available") {
                        if(f.Gpu.Items.Count==0 || f.Status.Text.Contains("설치가 필요") || f.Status.Text.Contains("공유 DLL")) throw new Exception("Global shared runtime should allow GPU enumeration without installation");
                    } else if(!(f.Status.Text.Contains("FFmpeg 구성 설치") || f.Status.Text.Contains("공유 DLL")) || f.Gpu.Items.Count!=0) throw new Exception("Missing runtime must show installation before invoking engine");
                    using(Bitmap image=new Bitmap(f.Width,f.Height)) {f.DrawToBitmap(image,new Rectangle(0,0,f.Width,f.Height));image.Save(args[0]);}
                    File.WriteAllText(args[0]+".txt","PASS: runtime availability UI");result=0;
                }catch(Exception e) {File.WriteAllText(args[0]+".txt",e.ToString());}
                f.Close();
            };
            Application.Run(f);
        }
        return result;
    }
}
