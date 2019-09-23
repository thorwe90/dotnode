export const dotnode: {
  initialize(runtimePath: string): void;
  invokeMethod(
    assemblyName: string,
    className: string,
    methodName: string,
    jsonArgs?: string
  ): string;
  shutdown(): void;
  isInitialized(): boolean;
};

export function dotnodeInvoke<TArgs, TResult>(
  assemblyName: string,
  className: string,
  methodName: string,
  args?: TArgs
): TResult;
