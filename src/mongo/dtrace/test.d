mongodb*:::message_handler_start
{
    printf("%d", tid);
}

mongodb*:::message_handler_done
{
    printf("%d", tid);

}

mongodb*:::command_start
{
    printf("%s", copyinstr(arg0));
}

mongodb*:::command_done
{
    printf("%d", arg0);

}

