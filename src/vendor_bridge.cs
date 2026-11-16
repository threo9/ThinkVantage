using System;
using System.IO;
using System.Reflection;
using System.Collections;
using System.Collections.Generic;
using System.Text;
using System.Web.Script.Serialization;
using System.Runtime.InteropServices;

// Calls the user's extracted, original Lenovo assemblies in an isolated x64
// .NET Framework process. The Qt UI never loads mixed-mode vendor assemblies.
class VendorBridge {
    [DllImport("PowerBattery.dll",EntryPoint="?DoesSupportStorage80@CChargingMode@PowerBattery@@SAHXZ",CallingConvention=CallingConvention.Cdecl)]
    static extern int Storage80();
    static readonly string Root = AppDomain.CurrentDomain.BaseDirectory;
    static readonly JavaScriptSerializer Json = new JavaScriptSerializer();
    static readonly List<Dictionary<string,object>> Diagnostics = new List<Dictionary<string,object>>();
    static void Error(string stage, Exception error) {
        Diagnostics.Add(new Dictionary<string,object> {
            {"provider", "Lenovo IdeaBatteryAgent"}, {"stage", stage},
            {"error", error.GetBaseException().Message}
        });
    }
    static Type TypeOf(string file, string type) { return Assembly.LoadFrom(Path.Combine(Root, file)).GetType(type, true); }
    static object Get(object obj, string key) { return obj == null ? null : obj.GetType().GetProperty(key).GetValue(obj, null); }
    static object Call(object obj, string method, params object[] args) { return obj.GetType().GetMethod(method).Invoke(obj, args); }
    static object Agent(string name) { return TypeOf("Idea" + name + "Agent.dll", "IdeaNotebookAddin." + name + "Agent").GetMethod("GetInstance").Invoke(null, null); }
    static object Request(string type) { return Activator.CreateInstance(TypeOf("PowerContract.dll", "Lenovo.Modern.Contracts.Power." + type)); }
    static void Set(object obj, string key, object value) {
        var prop = obj.GetType().GetProperty(key);
        var type = Nullable.GetUnderlyingType(prop.PropertyType) ?? prop.PropertyType;
        prop.SetValue(obj, type.IsEnum ? Enum.ToObject(type, value) : value, null);
    }
    static Dictionary<string,object> Fields(object value, params string[] keys) {
        var r = new Dictionary<string,object>();
        foreach (var key in keys) {
            try {
                var v = Get(value,key);
                if (v != null && v.GetType().IsEnum) v = Convert.ToInt32(v);
                // A date-only ISO value avoids the serializer's implementation-
                // specific DateTimeOffset object layout. Unknown stays null.
                if (v is DateTimeOffset) {
                    var date = (DateTimeOffset)v;
                    v = date == DateTimeOffset.MinValue ? null : (object)date.ToString("yyyy-MM-dd", System.Globalization.CultureInfo.InvariantCulture);
                }
                r[key] = v;
            } catch (Exception e) {
                r[key] = null;
                Error("field:" + key, e);
            }
        }
        return r;
    }
    static int Main(string[] args) {
        Console.OutputEncoding = new UTF8Encoding(false);
        Directory.SetCurrentDirectory(Root);
        var result = new Dictionary<string,object> {
            {"schema", 1}, {"provider", "Lenovo IdeaBatteryAgent"}, {"diagnostics", Diagnostics}
        };
        try {
            if (args.Length == 0 || args[0] == "probe") {
                try {
                    var response = Call(Agent("Battery"),"GetBatteryInformation");
                    var list = (IEnumerable)Get(response,"BatteryInformationList");
                    if (list == null) throw new InvalidOperationException("原厂电池接口未返回电池列表");
                    foreach (object b in list) {
                        result["battery"] = Fields(b,"FullChargeCapacity","RemainingCapacity","DesignCapacity","Temperature","CycleCount","DeviceChemistry","ChargeStatus","Voltage","Current","Wattage","AcWattage","AcAdapterStatus","BatteryHealth","ManufactureName","ManufactureDate","DesignVoltage");
                        break;
                    }
                    if (!result.ContainsKey("battery")) throw new InvalidOperationException("原厂电池列表为空");
                } catch (Exception e) { result["batteryError"] = e.GetBaseException().Message; Error("GetBatteryInformation", e); }
                // Failure to query this optional capability must not erase
                // successfully read battery fields or misreport false.
                try {
                    int storage = Storage80();
                    if (storage == 0 || storage == 1) result["storage80"] = storage == 1;
                    else throw new InvalidOperationException("原厂接口返回未知养护阈值能力");
                } catch (Exception e) { Error("DoesSupportStorage80", e); }
                result["observedAt"] = DateTimeOffset.UtcNow.ToString("o", System.Globalization.CultureInfo.InvariantCulture);
                Console.WriteLine(Json.Serialize(result)); return 0;
            }
            throw new ArgumentException("Unknown command");
        } catch(Exception e) { result["ok"]=false;result["error"]=e.GetBaseException().Message; }
        Console.WriteLine(Json.Serialize(result));return result.ContainsKey("ok") && !(bool)result["ok"] ? 1 : 0;
    }
}
