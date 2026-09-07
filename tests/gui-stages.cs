using System;
using System.Drawing;
using System.IO;
using System.Reflection;
using System.Windows.Forms;
internal static class GuiStagesTest {
    static void Receive(HdrWindow f,string line) {
        typeof(HdrWindow).GetMethod("Receive",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(f,new object[]{line});
    }
    static void Require(bool value,string name) {if(!value) throw new Exception(name);}
    [STAThread] static int Main(string[] args) {
        Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);
        int result=1;
        using(HdrWindow f=new HdrWindow(args[0]+".ini")) {
            f.Language.SelectedIndex=1;
            f.Opacity=0;f.ShowInTaskbar=false;
            f.Shown+=delegate {
                try {
                    Receive(f,"240 frames / ~240, recent 40.0 fps, average 35.0 fps");
                    Receive(f,"RTXHDR_STAGE video_finalize");
                    Require(f.Status.Text.Contains("인코더 마무리"),"Encoder finalization stage");
                    Receive(f,"RTXHDR_STAGE mux_aac");
                    Require(f.Status.Text.Contains("오디오 muxing"),"Audio mux stage");
                    Receive(f,"240 frames / ~240, recent 40.0 fps, average 35.0 fps");
                    Require(f.Status.Text.Contains("오디오 muxing"),"Late progress must not reset stage");
                    Require(!f.Succeeded && !f.Finished,"Mux must not imply overall success");
                    f.Refresh();
                    using(Bitmap image=new Bitmap(f.Width,f.Height)) {f.DrawToBitmap(image,new Rectangle(0,0,f.Width,f.Height));image.Save(args[0]);}
                    Receive(f,"RTXHDR_STAGE verify");
                    Require(f.Status.Text.Contains("결과 검사"),"Verification stage");
                    Receive(f,"RTXHDR_STAGE finalize");
                    Require(f.Status.Text.Contains("저장 마무리"),"Final save stage");
                    Receive(f,"RTXHDR_STAGE cleanup");
                    Require(f.Status.Text.Contains("중간 파일 정리"),"Cleanup stage");
                    Require(!f.Succeeded && !f.Finished,"Cleanup stage must not imply process completion");
                    Require(String.Join(",",f.StageHistory)=="video_finalize,mux_aac,verify,finalize,cleanup","Stage order");
                    using(var pending=new System.Diagnostics.Process()) {
                        f.Running=pending;
                        try {f.CancelConversion();Require(!(bool)typeof(HdrWindow).GetField("cancelled",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(f),"Published output cleanup must finish when closing/cancelling");}
                        finally {f.Running=null;}
                    }
                    Receive(f,"RTXHDR_CLEANUP_WARNING Locked intermediate");
                    File.WriteAllText(args[0]+".complete","published output fixture");
                    typeof(HdrWindow).GetField("completedOutput",BindingFlags.Instance|BindingFlags.NonPublic).SetValue(f,args[0]+".complete");
                    f.Running=new System.Diagnostics.Process();
                    typeof(HdrWindow).GetMethod("Complete",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(f,new object[]{0});
                    Require(f.Succeeded && f.Finished && f.Status.Text.Contains("정리 일부 남음"),"Cleanup warning must preserve successful conversion state");
                    result=0;File.WriteAllText(args[0]+".txt","PASS stage state, stale progress, completion boundary, cleanup cancellation/warning and layout");
                } catch(Exception e) {File.WriteAllText(args[0]+".txt",e.ToString());}
                f.Close();
            };
            Application.Run(f);
        }
        return result;
    }
}
