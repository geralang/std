
function std_time_utc_unix_millis() {
    return BigInt(new Date().getTime());
}

function std_time_local_unix_millis() {
    const d = new Date();
    return BigInt(d.getTime()) - BigInt(d.getTimezoneOffset()) * 60n * 1000n;
}