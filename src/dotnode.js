var dotnode = require("bindings")("dotnode");

var dotnodeInvoke = function(assemblyName, className, methodName, args) {
  var jsonInvokeArgs = undefined;
  if (args) {
    jsonInvokeArgs = JSON.stringify(args);
  }

  var result = dotnode.invokeMethod(
    assemblyName,
    className,
    methodName,
    jsonInvokeArgs
  );

  if (result) {
    return JSON.parse(result);
  }

  return undefined;
};

module.exports = {
  dotnode,
  dotnodeInvoke
};
