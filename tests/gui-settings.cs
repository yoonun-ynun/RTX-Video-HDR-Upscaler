using System;
using System.IO;
using System.Reflection;
using System.Windows.Forms;
internal static class GuiSettingsTest {
    static NumericUpDown Number(HdrWindow f,string field) {
        return (NumericUpDown)typeof(HdrWindow).GetField(field,BindingFlags.Instance|BindingFlags.NonPublic).GetValue(f);
    }
    static void Require(bool value,string name) {if(!value) throw new Exception(name);}
    [STAThread] static int Main(string[] args) {
        Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);
        string path=args[0],operation=args[1];int result=1;
        if(operation=="invalid") File.WriteAllText(path,"[output]\nmode=invalid\ncq=999\nbitrate_mbps=oops\ncontainer=invalid\ngpu_index=999\ngpu_name=Missing GPU\n");
        using(HdrWindow f=new HdrWindow(path)) {
            f.Opacity=0;f.ShowInTaskbar=false;
            f.Shown+=delegate {
                try {
                    if(operation=="write") {
                        f.mode.SelectedIndex=1;Number(f,"cq").Value=22;Number(f,"bitrate").Value=75.5M;
                        f.FormatChoice.SelectedIndex=1;f.Gpu.SelectedIndex=Math.Min(1,f.Gpu.Items.Count-1);
                        var saved=GuiSettings.Load(path);
                        Require(saved.Mode==1 && saved.Cq==22 && saved.Bitrate==75.5M && saved.Format==1,"Immediate save");
                    } else if(operation=="read") {
                        Require(f.mode.SelectedIndex==1 && Number(f,"cq").Value==22 && Number(f,"bitrate").Value==75.5M && f.FormatChoice.SelectedIndex==1,"Settings survived process restart");
                        Require(f.Gpu.SelectedIndex==Math.Min(1,f.Gpu.Items.Count-1),"GPU restored");
                    } else {
                        Require(f.mode.SelectedIndex==0 && Number(f,"cq").Value==18 && Number(f,"bitrate").Value==40 && f.FormatChoice.SelectedIndex==0,"Invalid settings fallback");
                        Require(f.Gpu.SelectedIndex==0,"Missing GPU fallback");
                    }
                    result=0;File.WriteAllText(path+"."+operation+".txt","PASS "+operation);
                } catch(Exception e) {File.WriteAllText(path+"."+operation+".txt",e.ToString());}
                f.Close();
            };
            Application.Run(f);
        }
        return result;
    }
}
