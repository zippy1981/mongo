//
// simple runner to run toplevel tests in jstests
//
var files = listFiles("jstests");

files.forEach(
    function(x) {
        
        if ( /[\/\\]_/.test(x.name) ||
             ! /\.js$/.test(x.name ) ){ 
            print(" >>>>>>>>>>>>>>> skipping " + x.name);
            return;
        }
        
        
        print(" *******************************************");
        print("         Test : " + x.name + " ...");
        print("                " + Date.timeFunc( function() { load(x.name); }, 1) + "ms");
        
    }
);


